#include "toi/viewport/x11_vulkan_surface.hpp"

#include <algorithm>
#include <utility>

#include <X11/Xlib.h>

namespace toi::viewport {

Result<X11Connection> X11Connection::open()
{
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return std::unexpected(make_error("XOpenDisplay(nullptr) failed; is DISPLAY set to an X/Xwayland server?"));
    }
    X11Connection connection;
    connection.display_ = display;
    return connection;
}

X11Connection::X11Connection(X11Connection&& other) noexcept
    : display_(std::exchange(other.display_, nullptr))
{
}

X11Connection& X11Connection::operator=(X11Connection&& other) noexcept
{
    if (this != &other) {
        reset();
        display_ = std::exchange(other.display_, nullptr);
    }
    return *this;
}

X11Connection::~X11Connection()
{
    reset();
}

void* X11Connection::display() const
{
    return display_;
}

Result<NativeSurfaceHandle> X11Connection::surface_handle(unsigned long x_window) const
{
    if (display_ == nullptr) {
        return std::unexpected(make_error("X11 connection is not open"));
    }
    if (x_window == 0) {
        return std::unexpected(make_error("native viewport window handle is null"));
    }

    auto* display = static_cast<Display*>(display_);
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, static_cast<Window>(x_window), &attributes) == 0) {
        return std::unexpected(make_error("XGetWindowAttributes failed for the native viewport window"));
    }

    return NativeSurfaceHandle{
        .x_display = display_,
        .x_window = x_window,
        .width = std::max(1, attributes.width),
        .height = std::max(1, attributes.height),
    };
}

void X11Connection::reset()
{
    if (display_ != nullptr) {
        XCloseDisplay(static_cast<Display*>(display_));
        display_ = nullptr;
    }
}

} // namespace toi::viewport
