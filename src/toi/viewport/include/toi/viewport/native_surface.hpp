#pragma once

#include "toi/viewport/error.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace toi::viewport {

struct NativeSurfaceExtent {
    int width = 0;
    int height = 0;
};

// Platform-owned native window and Vulkan surface operations. The Linux build
// implements this module with X11; future platforms replace only its implementation.
class NativeSurface {
public:
    [[nodiscard]] static Result<NativeSurface> attach(std::uintptr_t native_window);
    [[nodiscard]] static std::span<const char* const> required_vulkan_instance_extensions();

    NativeSurface() = default;
    NativeSurface(const NativeSurface&) = delete;
    NativeSurface& operator=(const NativeSurface&) = delete;
    NativeSurface(NativeSurface&& other) noexcept;
    NativeSurface& operator=(NativeSurface&& other) noexcept;
    ~NativeSurface();

    [[nodiscard]] Result<VkSurfaceKHR> create_vulkan_surface(VkInstance instance) const;
    [[nodiscard]] Result<NativeSurfaceExtent> extent() const;

private:
    void reset();

    void* connection_ = nullptr;
    std::uintptr_t window_ = 0;
};

} // namespace toi::viewport
