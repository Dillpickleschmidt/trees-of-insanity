#include "toi/ovrtx/renderer_session.hpp"

#include "toi/ovrtx/environment.hpp"

#include <ovrtx/ovrtx.h>
#include <ovrtx/ovrtx_attributes.h>
#include <ovrtx/ovrtx_types.h>

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace toi::ovrtx {
namespace {

constexpr int kWarmupFramesAfterStageOpen = 1;

static_assert(sizeof(growth::Vec3) == sizeof(float) * 3);

[[nodiscard]] Result<void> write_mesh_points(ovrtx_renderer_t* renderer,
                                             const std::vector<render::GrowthPreviewMeshAttributes>& meshes)
{
    if (meshes.empty()) {
        return {};
    }

    std::vector<ovx_string_t> paths;
    std::vector<std::size_t> point_counts;
    std::vector<DLTensor> tensors;
    paths.reserve(meshes.size());
    point_counts.reserve(meshes.size());
    tensors.reserve(meshes.size());

    const DLDataType point_type{.code = kDLFloat, .bits = 32, .lanes = 3};
    for (const auto& mesh : meshes) {
        paths.push_back(to_ovx_string(mesh.prim_path));
        point_counts.push_back(mesh.points.size());
        tensors.push_back(ovrtx_make_write_cpu_tensor(mesh.points.data(), &point_counts.back(), point_type));
    }

    auto binding =
        ovrtx_make_binding_desc(paths.data(), paths.size(), to_ovx_string("points"), OVRTX_SEMANTIC_NONE, point_type);
    binding.binding_desc.attribute_type.is_array = true;

    ovrtx_input_buffer_t buffer{};
    buffer.tensors = tensors.data();
    buffer.tensor_count = tensors.size();

    return wait_for_operation(renderer, ovrtx_write_attribute(renderer, &binding, &buffer, OVRTX_DATA_ACCESS_SYNC),
                              "ovrtx_write_attribute points failed");
}

} // namespace

RendererSession::RendererSession(RendererHandle renderer, CudaStream stream, RendererSessionOptions options)
    : renderer_(std::move(renderer))
    , stream_(std::move(stream))
    , options_(std::move(options))
{
}

RendererSession::~RendererSession()
{
    auto synchronized = stream_.synchronize();
    (void)synchronized;
}

Result<RendererSession> RendererSession::create(RendererSessionOptions options)
{
    ensure_ovrtx_environment();
    prepend_usd_asset_search_path(options.asset_search_path);

    auto stream = CudaStream::create(options.cuda_device);
    if (!stream) {
        return std::unexpected(stream.error());
    }

    RendererOptions renderer_options;
    renderer_options.sync_mode = true;
    renderer_options.active_cuda_gpus = options.active_cuda_gpus;
    auto renderer = create_renderer(renderer_options);
    if (!renderer) {
        return std::unexpected(renderer.error());
    }
    return RendererSession(std::move(*renderer), std::move(*stream), std::move(options));
}

Result<void> RendererSession::submit_growth_preview(const render::GrowthPreviewStageProjection& stage)
{
    if (loaded_stage_usda_ != stage.usd_stage.text) {
        auto opened = wait_for_operation(
            renderer_.get(), ovrtx_open_usd_from_string(renderer_.get(), to_ovx_string(stage.usd_stage.text)),
            "ovrtx_open_usd_from_string failed");
        if (!opened) {
            loaded_stage_usda_.clear();
            return std::unexpected(opened.error());
        }
        loaded_stage_usda_ = stage.usd_stage.text;
        pending_warmup_frames_ = kWarmupFramesAfterStageOpen;
    }

    auto written = write_mesh_points(renderer_.get(), stage.mesh_attributes);
    if (!written) {
        return std::unexpected(written.error());
    }

    render_product_path_ = stage.usd_stage.render_product_path;
    camera_path_ = stage.usd_stage.camera_path;
    auto camera_written = set_growth_preview_camera(stage.camera);
    if (!camera_written) {
        return std::unexpected(camera_written.error());
    }
    return {};
}

Result<void> RendererSession::set_growth_preview_camera(render::GrowthPreviewCamera camera)
{
    const auto path = to_ovx_string(camera_path_);
    const auto matrix = render::growth_preview_camera_transform_matrix(camera);
    ovrtx_xform_matrix44d_t transform{};
    for (std::size_t index = 0; index < matrix.size(); ++index) {
        transform.v[index] = matrix[index];
    }

    auto written = wait_for_operation(renderer_.get(), ovrtx_set_xform_mat(renderer_.get(), &path, 1, &transform),
                                      "ovrtx_set_xform_mat camera failed");
    if (!written) {
        return std::unexpected(written.error());
    }
    camera_ = camera;
    return {};
}

Result<MappedCudaFrame> RendererSession::render_cuda_frame(RenderFrameOutputs requested_outputs,
                                                           std::uintptr_t sync_stream)
{
    if (sync_stream == 0) {
        sync_stream = stream_.ovrtx_handle();
    }
    while (pending_warmup_frames_ > 0) {
        --pending_warmup_frames_;
        auto discarded = render_cuda_frame_once(RenderFrameOutputs::Color, sync_stream);
        if (!discarded) {
            return std::unexpected(discarded.error());
        }
    }
    return render_cuda_frame_once(requested_outputs, sync_stream);
}

Result<MappedCudaFrame> RendererSession::render_cuda_frame_once(RenderFrameOutputs requested_outputs,
                                                                std::uintptr_t sync_stream)
{
    const auto render_product = to_ovx_string(render_product_path_);
    ovrtx_render_product_set_t render_products = {};
    render_products.render_products = &render_product;
    render_products.num_render_products = 1;

    ovrtx_step_result_handle_t step_handle = OVRTX_INVALID_HANDLE;
    const auto stepped = ovrtx_step(renderer_.get(), render_products, options_.delta_seconds, &step_handle);
    if (api_failed(stepped) || step_handle == OVRTX_INVALID_HANDLE) {
        return std::unexpected(make_error(last_error_message("ovrtx_step failed")));
    }

    auto waited = wait_for_operation(renderer_.get(), stepped, "ovrtx_step failed");
    if (!waited) {
        if (step_handle != OVRTX_INVALID_HANDLE) {
            ovrtx_destroy_results(renderer_.get(), step_handle);
        }
        return std::unexpected(waited.error());
    }

    ResultsHandle results(renderer_.get(), step_handle);
    ovrtx_render_product_set_outputs_t render_outputs = {};
    const auto fetched = ovrtx_fetch_results(renderer_.get(), step_handle, ovrtx_timeout_infinite, &render_outputs);
    if (api_failed(fetched)) {
        return std::unexpected(make_error(last_error_message("ovrtx_fetch_results failed")));
    }
    if (render_outputs.status != OVRTX_EVENT_COMPLETED) {
        const auto message = from_ovx_string(render_outputs.error_message);
        return std::unexpected(make_error(message.empty() ? "ovrtx outputs were not completed" : message));
    }

    const auto ldr_handle = find_render_var_output(render_outputs, "LdrColor");
    if (ldr_handle == OVRTX_INVALID_HANDLE) {
        return std::unexpected(make_error("LdrColor output was not produced"));
    }
    ovrtx_map_output_description_t map_desc = {};
    map_desc.device_type = OVRTX_MAP_DEVICE_TYPE_CUDA;
    map_desc.sync_stream = sync_stream;

    ovrtx_render_var_output_t mapped = {};
    const auto mapped_result =
        ovrtx_map_render_var_output(renderer_.get(), ldr_handle, &map_desc, ovrtx_timeout_infinite, &mapped);
    if (api_failed(mapped_result)) {
        return std::unexpected(make_error(last_error_message("ovrtx_map_render_var_output failed")));
    }

    MappedOutputHandle output(renderer_.get(), mapped.map_handle);
    output.set_before_destroy_stream(sync_stream);
    auto tensor = require_ldr_tensor_view(mapped);
    if (!tensor) {
        return std::unexpected(tensor.error());
    }
    if (tensor->tensor->device.device_type != kDLCUDA) {
        return std::unexpected(make_error("LdrColor CUDA map did not return a CUDA tensor"));
    }

    MappedOutputHandle distance_output;
    const void* scene_distance_cuda_array = nullptr;
    if (requested_outputs == RenderFrameOutputs::ColorAndDistance) {
        const auto distance_handle = find_render_var_output(render_outputs, "DistanceToCameraSD");
        if (distance_handle == OVRTX_INVALID_HANDLE) {
            return std::unexpected(make_error("DistanceToCameraSD output was not produced"));
        }

        ovrtx_map_output_description_t distance_map_desc = {};
        distance_map_desc.device_type = OVRTX_MAP_DEVICE_TYPE_CUDA_ARRAY;
        distance_map_desc.sync_stream = sync_stream;
        ovrtx_render_var_output_t mapped_distance = {};
        const auto mapped_distance_result = ovrtx_map_render_var_output(
            renderer_.get(), distance_handle, &distance_map_desc, ovrtx_timeout_infinite, &mapped_distance);
        if (api_failed(mapped_distance_result)) {
            return std::unexpected(make_error(last_error_message("DistanceToCameraSD map failed")));
        }

        distance_output = MappedOutputHandle(renderer_.get(), mapped_distance.map_handle);
        distance_output.set_before_destroy_stream(sync_stream);
        auto distance_tensor = require_float_cuda_array_tensor_view(mapped_distance, "DistanceToCameraSD");
        if (!distance_tensor) {
            return std::unexpected(distance_tensor.error());
        }
        if (distance_tensor->width != tensor->width || distance_tensor->height != tensor->height) {
            return std::unexpected(make_error("DistanceToCameraSD dimensions do not match LdrColor"));
        }

        scene_distance_cuda_array = distance_tensor->cuda_array;
    }

    // ovrtx renders asynchronously on its own CUDA stream, and ovrtx_step /
    // ovrtx_fetch_results do not guarantee that render has finished on the GPU
    // when they return. The output map targets sync_stream, but that does not
    // cover ovrtx's render stream, so a caller that immediately reads the mapped
    // LdrColor/distance on sync_stream can race the render and get a partly- or
    // un-rendered (black) frame. Wait for the device so the returned frame's
    // pixels are complete. (Paced callers only hid this by giving the render
    // incidental time to land.)
    if (auto synced = require_cuda_success(cudaDeviceSynchronize(), "cudaDeviceSynchronize after ovrtx render");
        !synced) {
        return std::unexpected(synced.error());
    }

    return MappedCudaFrame{
        .results = std::move(results),
        .output = std::move(output),
        .distance_output = std::move(distance_output),
        .tensor = tensor->tensor,
        .width = tensor->width,
        .height = tensor->height,
        .channel_count = tensor->channel_count,
        .row_stride_bytes = tensor->row_stride_bytes,
        .byte_offset = tensor->byte_offset,
        .sync_stream = sync_stream,
        .camera = camera_,
        .scene_distance_cuda_array = scene_distance_cuda_array,
    };
}

} // namespace toi::ovrtx
