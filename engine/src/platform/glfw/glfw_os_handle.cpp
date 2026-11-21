// The OS handle of the window, kept apart from the rest of the GLFW window: glfw3native.h brings in the OS window headers,
// and X11's define macros such as None and CursorShape, which would break any code that comes after them.

#include "glfw_window.hpp"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
// CI builds GLFW without Wayland
#if __has_include(<wayland-client.h>)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#endif
#include <GLFW/glfw3native.h>

namespace crimson::glfw
{
    OSWindowHandle GLFWWindow::GetOSHandle() const
    {
#if defined(_WIN32)
        return { OSWindowHandle::Kind::Win32, reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(m_handle)) };
#elif defined(__APPLE__)
        return { OSWindowHandle::Kind::Cocoa, reinterpret_cast<std::uintptr_t>(glfwGetCocoaWindow(m_handle)) };
#else
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            return { OSWindowHandle::Kind::Wayland, reinterpret_cast<std::uintptr_t>(glfwGetWaylandWindow(m_handle)) };
#endif
        return { OSWindowHandle::Kind::X11, static_cast<std::uintptr_t>(glfwGetX11Window(m_handle)) };
#endif
    }
}
