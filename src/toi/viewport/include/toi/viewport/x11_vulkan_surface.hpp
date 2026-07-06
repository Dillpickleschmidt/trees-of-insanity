#pragma once

#include "toi/viewport/error.hpp"

namespace toi::viewport {

// A native X11 window surface handle for Vulkan. x_display is a Display* owned
// by the caller (see X11Connection); x_window is a server-global window XID
// handed over by the shell (the Electrobun WGPU child window).
struct NativeSurfaceHandle {
    void* x_display = nullptr;
    unsigned long x_window = 0;
    int width = 0;
    int height = 0;
};

// A private connection to the X server (XOpenDisplay(nullptr)). The native core
// opens its own connection rather than borrowing the shell's, because window
// XIDs are valid across connections to the same server.
class X11Connection {
public:
    [[nodiscard]] static Result<X11Connection> open();

    X11Connection() = default;
    X11Connection(const X11Connection&) = delete;
    X11Connection& operator=(const X11Connection&) = delete;
    X11Connection(X11Connection&& other) noexcept;
    X11Connection& operator=(X11Connection&& other) noexcept;
    ~X11Connection();

    [[nodiscard]] void* display() const;

    // Validates the window on this connection and reads its current geometry.
    [[nodiscard]] Result<NativeSurfaceHandle> surface_handle(unsigned long x_window) const;

private:
    void reset();

    void* display_ = nullptr;
};

} // namespace toi::viewport
