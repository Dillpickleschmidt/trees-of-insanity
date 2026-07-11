#include "toi/ovrtx/handles.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace toi::ovrtx {

RendererHandle::RendererHandle(ovrtx_renderer_t* renderer)
    : renderer_(renderer)
{
}

RendererHandle::RendererHandle(RendererHandle&& other) noexcept
    : renderer_(std::exchange(other.renderer_, nullptr))
{
}

RendererHandle& RendererHandle::operator=(RendererHandle&& other) noexcept
{
    if (this != &other) {
        reset();
        renderer_ = std::exchange(other.renderer_, nullptr);
    }
    return *this;
}

RendererHandle::~RendererHandle()
{
    reset();
}

ovrtx_renderer_t* RendererHandle::get() const
{
    return renderer_;
}

void RendererHandle::reset()
{
    if (renderer_ != nullptr) {
        ovrtx_destroy_renderer(renderer_);
        renderer_ = nullptr;
    }
}

ResultsHandle::ResultsHandle(ovrtx_renderer_t* renderer, ovrtx_step_result_handle_t handle)
    : renderer_(renderer)
    , handle_(handle)
{
}

ResultsHandle::ResultsHandle(ResultsHandle&& other) noexcept
    : renderer_(std::exchange(other.renderer_, nullptr))
    , handle_(std::exchange(other.handle_, OVRTX_INVALID_HANDLE))
{
}

ResultsHandle& ResultsHandle::operator=(ResultsHandle&& other) noexcept
{
    if (this != &other) {
        reset();
        renderer_ = std::exchange(other.renderer_, nullptr);
        handle_ = std::exchange(other.handle_, OVRTX_INVALID_HANDLE);
    }
    return *this;
}

ResultsHandle::~ResultsHandle()
{
    reset();
}

void ResultsHandle::reset()
{
    if (renderer_ != nullptr && handle_ != OVRTX_INVALID_HANDLE) {
        ovrtx_destroy_results(renderer_, handle_);
        handle_ = OVRTX_INVALID_HANDLE;
    }
}

MappedOutputHandle::MappedOutputHandle(ovrtx_renderer_t* renderer, ovrtx_render_var_output_map_handle_t handle)
    : renderer_(renderer)
    , handle_(handle)
{
}

MappedOutputHandle::MappedOutputHandle(MappedOutputHandle&& other) noexcept
    : renderer_(std::exchange(other.renderer_, nullptr))
    , handle_(std::exchange(other.handle_, OVRTX_INVALID_HANDLE))
    , before_destroy_sync_(std::exchange(other.before_destroy_sync_, {}))
{
}

MappedOutputHandle& MappedOutputHandle::operator=(MappedOutputHandle&& other) noexcept
{
    if (this != &other) {
        reset();
        renderer_ = std::exchange(other.renderer_, nullptr);
        handle_ = std::exchange(other.handle_, OVRTX_INVALID_HANDLE);
        before_destroy_sync_ = std::exchange(other.before_destroy_sync_, {});
    }
    return *this;
}

MappedOutputHandle::~MappedOutputHandle()
{
    reset();
}

void MappedOutputHandle::set_before_destroy_stream(std::uintptr_t stream)
{
    before_destroy_sync_.stream = stream;
}

void MappedOutputHandle::reset()
{
    if (renderer_ != nullptr && handle_ != OVRTX_INVALID_HANDLE) {
        ovrtx_unmap_render_var_output(renderer_, handle_, before_destroy_sync_);
        handle_ = OVRTX_INVALID_HANDLE;
        before_destroy_sync_ = {};
    }
}

Result<RendererHandle> create_renderer(const RendererOptions& options)
{
    const auto cuda_gpus = to_ovx_string(options.active_cuda_gpus);
    std::array<ovrtx_config_entry_t, 4> entries = {
        ovrtx_config_entry_sync_mode(options.sync_mode),
        ovrtx_config_entry_keep_system_alive(options.keep_system_alive),
        ovrtx_config_entry_use_vulkan(options.use_vulkan),
        ovrtx_config_entry_active_cuda_gpus(cuda_gpus),
    };
    ovrtx_config_t config = {};
    config.entries = entries.data();
    config.entry_count = entries.size();

    ovrtx_renderer_t* renderer = nullptr;
    const auto result = ovrtx_create_renderer(&config, &renderer);
    if (api_failed(result) || renderer == nullptr) {
        return std::unexpected(make_error(last_error_message("ovrtx_create_renderer failed")));
    }
    return RendererHandle(renderer);
}

Result<void> wait_for_operation(ovrtx_renderer_t* renderer, ovrtx_enqueue_result_t operation, std::string_view context)
{
    if (api_failed(operation)) {
        return std::unexpected(make_error(last_error_message(context)));
    }
    if (operation.op_index == OVRTX_INVALID_HANDLE) {
        return {};
    }

    ovrtx_op_wait_result_t wait_result = {};
    const auto wait = ovrtx_wait_op(renderer, operation.op_index, ovrtx_timeout_infinite, &wait_result);
    if (api_failed(wait)) {
        return std::unexpected(make_error(last_error_message(context)));
    }
    if (wait_result.num_error_ops > 0 && wait_result.error_op_ids != nullptr) {
        const auto op_error = from_ovx_string(ovrtx_get_last_op_error(wait_result.error_op_ids[0]));
        return std::unexpected(make_error(op_error.empty() ? std::string(context) : op_error));
    }
    return {};
}

bool api_failed(ovrtx_result_t result)
{
    return result.status != OVRTX_API_SUCCESS;
}

bool api_failed(ovrtx_enqueue_result_t result)
{
    return result.status != OVRTX_API_SUCCESS;
}

} // namespace toi::ovrtx
