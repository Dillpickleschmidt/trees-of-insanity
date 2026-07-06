#pragma once

#include "toi/ovrtx/error.hpp"

#include <ovrtx/ovrtx.h>
#include <ovrtx/ovrtx_config.h>
#include <ovrtx/ovrtx_types.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace toi::ovrtx {

struct RendererOptions {
    bool sync_mode = true;
    bool keep_system_alive = true;
    bool use_vulkan = true;
    std::string active_cuda_gpus = "0";
};

class RendererHandle {
public:
    RendererHandle() = default;
    explicit RendererHandle(ovrtx_renderer_t* renderer);
    RendererHandle(const RendererHandle&) = delete;
    RendererHandle& operator=(const RendererHandle&) = delete;
    RendererHandle(RendererHandle&& other) noexcept;
    RendererHandle& operator=(RendererHandle&& other) noexcept;
    ~RendererHandle();

    [[nodiscard]] ovrtx_renderer_t* get() const;

private:
    void reset();

    ovrtx_renderer_t* renderer_ = nullptr;
};

class ResultsHandle {
public:
    ResultsHandle() = default;
    ResultsHandle(ovrtx_renderer_t* renderer, ovrtx_step_result_handle_t handle);
    ResultsHandle(const ResultsHandle&) = delete;
    ResultsHandle& operator=(const ResultsHandle&) = delete;
    ResultsHandle(ResultsHandle&& other) noexcept;
    ResultsHandle& operator=(ResultsHandle&& other) noexcept;
    ~ResultsHandle();

private:
    void reset();

    ovrtx_renderer_t* renderer_ = nullptr;
    ovrtx_step_result_handle_t handle_ = OVRTX_INVALID_HANDLE;
};

class MappedOutputHandle {
public:
    MappedOutputHandle() = default;
    MappedOutputHandle(ovrtx_renderer_t* renderer, ovrtx_render_var_output_map_handle_t handle);
    MappedOutputHandle(const MappedOutputHandle&) = delete;
    MappedOutputHandle& operator=(const MappedOutputHandle&) = delete;
    MappedOutputHandle(MappedOutputHandle&& other) noexcept;
    MappedOutputHandle& operator=(MappedOutputHandle&& other) noexcept;
    ~MappedOutputHandle();

    void set_before_destroy_stream(std::uintptr_t stream);

private:
    void reset();

    ovrtx_renderer_t* renderer_ = nullptr;
    ovrtx_render_var_output_map_handle_t handle_ = OVRTX_INVALID_HANDLE;
    ovrtx_cuda_sync_t before_destroy_sync_{};
};

[[nodiscard]] Result<RendererHandle> create_renderer(const RendererOptions& options = {});
[[nodiscard]] Result<void> wait_for_operation(ovrtx_renderer_t* renderer, ovrtx_enqueue_result_t operation,
                                              std::string_view context);
[[nodiscard]] bool api_failed(ovrtx_result_t result);
[[nodiscard]] bool api_failed(ovrtx_enqueue_result_t result);

} // namespace toi::ovrtx
