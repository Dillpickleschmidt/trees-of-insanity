// Headless ovrtx smoke test: render one growth-preview frame to a CUDA buffer.
// Exercises the full ovrtx path (renderer create -> open USD stage -> write mesh
// points -> step -> map LdrColor + DistanceToCameraSD) without a window or Vulkan
// viewport. Requires an NVIDIA GPU and the ovrtx runtime on the library path.

#include "toi/model/desktop_session.hpp"
#include "toi/ovrtx/renderer_session.hpp"
#include "toi/render/render_projection.hpp"

#include <cuda_runtime_api.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <vector>

namespace {

std::filesystem::path prototype_path()
{
    return TOI_TEST_PROTOTYPE_ASSET_PATH;
}

std::filesystem::path asset_root_path()
{
    return TOI_TEST_ASSET_ROOT_PATH;
}

std::filesystem::path fresh_project_path(std::string_view name)
{
    const auto root = std::filesystem::temp_directory_path() / "trees-of-insanity-tests";
    std::filesystem::create_directories(root);
    auto path = root / (std::string(name) + ".toi.project.json");
    std::filesystem::remove(path);
    return path;
}

} // namespace

TEST_CASE("ovrtx renders a growth-preview frame to a CUDA buffer")
{
    auto session = toi::model::DesktopSession::create({
        .project_path = fresh_project_path("ovrtx-growth-preview"),
        .asset_root_path = asset_root_path(),
        .prototype_asset_path = prototype_path(),
    });
    REQUIRE(session.has_value());

    auto snapshot = session->module_preview_snapshot();
    REQUIRE(snapshot.has_value());
    const auto stage = toi::render::make_growth_preview_stage_projection(
        snapshot->snapshot, snapshot->camera_snapshot, snapshot->prepared_prototype,
        {.asset_search_path = asset_root_path()});
    CHECK(stage.mesh.vertex_count > 0);

    auto renderer = toi::ovrtx::RendererSession::create({.asset_search_path = asset_root_path()});
    INFO((renderer.has_value() ? std::string{} : renderer.error().message));
    REQUIRE(renderer.has_value());

    auto submitted = renderer->submit_growth_preview(stage);
    INFO((submitted.has_value() ? std::string{} : submitted.error().message));
    REQUIRE(submitted.has_value());

    // Render color + DistanceToCameraSD together, as the depth-aware guide
    // overlay does; requesting distance must not disturb the color output.
    auto frame = renderer->render_cuda_frame(toi::ovrtx::RenderFrameOutputs::ColorAndDistance);
    INFO((frame.has_value() ? std::string{} : frame.error().message));
    REQUIRE(frame.has_value());

    CHECK(frame->width == stage.usd_stage.width);
    CHECK(frame->height == stage.usd_stage.height);
    CHECK(frame->channel_count >= 4);
    REQUIRE(frame->color_cuda_array != nullptr);

    // Copy the CUDA color output to host and confirm the render is not black
    // (isolates ovrtx scene rendering from downstream Vulkan interop). Reads are
    // enqueued on sync_stream, which the renderer orders after render completion.
    const auto row_bytes = static_cast<std::size_t>(frame->width) * 4;
    std::vector<unsigned char> pixels(row_bytes * static_cast<std::size_t>(frame->height));
    REQUIRE(cudaMemcpy2DFromArrayAsync(pixels.data(), row_bytes,
                                       reinterpret_cast<cudaArray_const_t>(frame->color_cuda_array), 0, 0, row_bytes,
                                       static_cast<std::size_t>(frame->height), cudaMemcpyDeviceToHost,
                                       reinterpret_cast<cudaStream_t>(frame->sync_stream)) == cudaSuccess);
    REQUIRE(cudaStreamSynchronize(reinterpret_cast<cudaStream_t>(frame->sync_stream)) == cudaSuccess);

    const auto brightest = *std::ranges::max_element(pixels);
    unsigned long long total = 0;
    for (const auto value : pixels) {
        total += value;
    }
    INFO("brightest channel=" << static_cast<int>(brightest) << " total=" << total);
    CHECK(brightest > 0);

    // The R32_SFLOAT distance array is what the guide overlay samples to occlude
    // guide lines behind geometry. Confirm the ColorAndDistance path produces it
    // with finite positive distances to the plant/ground.
    REQUIRE(frame->scene_distance_cuda_array != nullptr);
    const auto distance_row_bytes = static_cast<std::size_t>(frame->width) * sizeof(float);
    std::vector<float> distances(static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height));
    REQUIRE(cudaMemcpy2DFromArray(distances.data(), distance_row_bytes,
                                  reinterpret_cast<cudaArray_const_t>(frame->scene_distance_cuda_array), 0, 0,
                                  distance_row_bytes, static_cast<std::size_t>(frame->height),
                                  cudaMemcpyDeviceToHost) == cudaSuccess);

    std::size_t finite_hits = 0;
    float nearest = std::numeric_limits<float>::max();
    for (const float distance : distances) {
        if (distance > 0.0F && distance < 3.402823e38F) {
            ++finite_hits;
            nearest = std::min(nearest, distance);
        }
    }
    INFO("finite distance texels=" << finite_hits << " nearest=" << nearest);
    CHECK(finite_hits > 0);
}
