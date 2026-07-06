// Headless ovrtx smoke test: render one growth-preview frame to a CUDA buffer.
// Exercises the full ovrtx path (renderer create -> open USD stage -> write mesh
// points -> step -> map LdrColor) without a window or Vulkan swapchain.
// Requires an NVIDIA GPU and the ovrtx runtime on the library path.

#include "toi/app/application_controller.hpp"
#include "toi/ovrtx/renderer_session.hpp"

#include <ovx/dlpack/dlpack.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

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
    auto controller = toi::app::ApplicationController::create({
        .project_path = fresh_project_path("ovrtx-growth-preview"),
        .asset_root_path = asset_root_path(),
        .prototype_asset_path = prototype_path(),
    });
    REQUIRE(controller.has_value());

    auto stage = controller->growth_preview_stage_projection();
    REQUIRE(stage.has_value());
    CHECK(stage->mesh.vertex_count > 0);

    auto session = toi::ovrtx::RendererSession::create({.asset_search_path = asset_root_path()});
    INFO((session.has_value() ? std::string{} : session.error().message));
    REQUIRE(session.has_value());

    auto submitted = session->submit_growth_preview(*stage);
    INFO((submitted.has_value() ? std::string{} : submitted.error().message));
    REQUIRE(submitted.has_value());

    auto frame = session->render_cuda_frame(toi::ovrtx::RenderFrameOutputs::Color);
    INFO((frame.has_value() ? std::string{} : frame.error().message));
    REQUIRE(frame.has_value());

    CHECK(frame->width == stage->usd_stage.width);
    CHECK(frame->height == stage->usd_stage.height);
    CHECK(frame->channel_count >= 4);
    REQUIRE(frame->tensor != nullptr);
    CHECK(frame->tensor->device.device_type == kDLCUDA);
    CHECK(frame->tensor->data != nullptr);
}
