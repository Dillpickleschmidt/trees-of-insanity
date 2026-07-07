#include "toi/ovrtx/output_mapping.hpp"

#include <ovx/dlpack/dlpack.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace toi::ovrtx {
namespace {

[[nodiscard]] bool same_string(ovx_string_t value, std::string_view expected)
{
    return value.ptr != nullptr && value.length == expected.size() &&
           std::char_traits<char>::compare(value.ptr, expected.data(), expected.size()) == 0;
}

} // namespace

ovrtx_render_var_output_handle_t find_render_var_output(const ovrtx_render_product_set_outputs_t& outputs,
                                                        std::string_view render_var_name)
{
    for (std::size_t product_index = 0; product_index < outputs.output_count; ++product_index) {
        const auto& product = outputs.outputs[product_index];
        for (std::size_t frame_index = 0; frame_index < product.output_frame_count; ++frame_index) {
            const auto& frame = product.output_frames[frame_index];
            for (std::size_t var_index = 0; var_index < frame.render_var_count; ++var_index) {
                const auto& render_var = frame.output_render_vars[var_index];
                if (same_string(render_var.render_var_name, render_var_name)) {
                    return render_var.output_handle;
                }
            }
        }
    }
    return OVRTX_INVALID_HANDLE;
}

Result<Rgba8CudaArrayTensorView> require_rgba8_cuda_array_tensor_view(const ovrtx_render_var_output_t& mapped,
                                                                      std::string_view render_var_name)
{
    if (mapped.status != OVRTX_EVENT_COMPLETED) {
        const auto message = from_ovx_string(mapped.error_message);
        return std::unexpected(
            make_error(message.empty() ? std::string(render_var_name) + " map was not completed" : message));
    }
    if (mapped.num_tensors != 1 || mapped.tensors == nullptr || mapped.tensors[0].dl == nullptr) {
        return std::unexpected(make_error(std::string(render_var_name) + " did not map to a single DLPack tensor"));
    }

    const DLTensor& tensor = *mapped.tensors[0].dl;
    if (tensor.ndim != 3 || tensor.shape == nullptr || tensor.shape[2] < 4 || tensor.dtype.lanes != 1 ||
        tensor.dtype.code != kDLOpaqueHandle || tensor.dtype.bits != 8 || tensor.data == nullptr ||
        tensor.device.device_type != kDLCUDA || tensor.byte_offset != 0) {
        return std::unexpected(
            make_error(std::string(render_var_name) + " tensor layout is not an RGBA8 CUDA array"));
    }

    const auto width = static_cast<int>(tensor.shape[1]);
    const auto height = static_cast<int>(tensor.shape[0]);
    const auto channel_count = static_cast<int>(tensor.shape[2]);
    if (width <= 0 || height <= 0) {
        return std::unexpected(make_error(std::string(render_var_name) + " tensor dimensions are invalid"));
    }

    return Rgba8CudaArrayTensorView{
        .cuda_array = tensor.data,
        .width = width,
        .height = height,
        .channel_count = channel_count,
    };
}

Result<FloatCudaArrayTensorView> require_float_cuda_array_tensor_view(const ovrtx_render_var_output_t& mapped,
                                                                      std::string_view render_var_name)
{
    if (mapped.status != OVRTX_EVENT_COMPLETED) {
        const auto message = from_ovx_string(mapped.error_message);
        return std::unexpected(
            make_error(message.empty() ? std::string(render_var_name) + " map was not completed" : message));
    }
    if (mapped.num_tensors != 1 || mapped.tensors == nullptr || mapped.tensors[0].dl == nullptr) {
        return std::unexpected(make_error(std::string(render_var_name) + " did not map to a single DLPack tensor"));
    }

    const DLTensor& tensor = *mapped.tensors[0].dl;
    if (tensor.ndim != 3 || tensor.shape == nullptr || tensor.shape[2] != 1 || tensor.dtype.lanes != 1 ||
        tensor.dtype.code != kDLOpaqueHandle || tensor.dtype.bits != 32 || tensor.data == nullptr ||
        tensor.device.device_type != kDLCUDA || tensor.byte_offset != 0) {
        return std::unexpected(
            make_error(std::string(render_var_name) + " tensor layout is not single-channel float CUDA array"));
    }

    const auto width = static_cast<int>(tensor.shape[1]);
    const auto height = static_cast<int>(tensor.shape[0]);
    if (width <= 0 || height <= 0) {
        return std::unexpected(make_error(std::string(render_var_name) + " tensor dimensions are invalid"));
    }

    return FloatCudaArrayTensorView{
        .tensor = &tensor,
        .cuda_array = tensor.data,
        .width = width,
        .height = height,
    };
}

} // namespace toi::ovrtx
