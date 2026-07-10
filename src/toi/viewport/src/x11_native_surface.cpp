#include "toi/viewport/native_surface.hpp"

#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>

namespace toi::viewport {

Result<NativeSurface> NativeSurface::attach(std::uintptr_t native_window)
{
    if (native_window == 0) {
        return std::unexpected(make_error("native viewport handle is null"));
    }
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        const char* display_name = std::getenv("DISPLAY");
        return std::unexpected(
            make_error(std::string("XOpenDisplay failed for DISPLAY=") + (display_name ? display_name : "<unset>")));
    }

    NativeSurface surface;
    surface.connection_ = display;
    surface.window_ = native_window;
    return surface;
}

std::span<const char* const> NativeSurface::required_vulkan_instance_extensions()
{
    static constexpr std::array extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
    return extensions;
}

NativeSurface::NativeSurface(NativeSurface&& other) noexcept
    : connection_(std::exchange(other.connection_, nullptr))
    , window_(std::exchange(other.window_, 0))
{
}

NativeSurface& NativeSurface::operator=(NativeSurface&& other) noexcept
{
    if (this != &other) {
        reset();
        connection_ = std::exchange(other.connection_, nullptr);
        window_ = std::exchange(other.window_, 0);
    }
    return *this;
}

NativeSurface::~NativeSurface()
{
    reset();
}

Result<VkSurfaceKHR> NativeSurface::create_vulkan_surface(VkInstance instance) const
{
    if (connection_ == nullptr || window_ == 0) {
        return std::unexpected(make_error("native surface is detached"));
    }
    VkXlibSurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    create_info.dpy = static_cast<Display*>(connection_);
    create_info.window = static_cast<Window>(window_);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const auto result = vkCreateXlibSurfaceKHR(instance, &create_info, nullptr, &surface);
    if (result != VK_SUCCESS) {
        return std::unexpected(
            make_error("vkCreateXlibSurfaceKHR failed: VkResult " + std::to_string(static_cast<int>(result))));
    }
    return surface;
}

Result<NativeSurfaceExtent> NativeSurface::extent() const
{
    if (connection_ == nullptr || window_ == 0) {
        return std::unexpected(make_error("native surface is detached"));
    }
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(static_cast<Display*>(connection_), static_cast<Window>(window_), &attributes) == 0) {
        return std::unexpected(make_error("XGetWindowAttributes failed for native viewport"));
    }
    return NativeSurfaceExtent{.width = std::max(1, attributes.width), .height = std::max(1, attributes.height)};
}

void NativeSurface::reset()
{
    if (connection_ != nullptr) {
        XCloseDisplay(static_cast<Display*>(connection_));
        connection_ = nullptr;
    }
    window_ = 0;
}

} // namespace toi::viewport
