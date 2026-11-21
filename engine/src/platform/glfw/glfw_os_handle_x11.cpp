// The X11 window of a GLFW window. A file per platform, as glfw3native.h needs the platform chosen before it is included.
// X11's headers define macros such as None and CursorShape, so nothing else is in this file.

#include "glfw_window.hpp"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

namespace crimson::glfw
{
    OSWindowHandle GLFWWindow::GetOSHandle() const
    {
        return { OSWindowHandle::Kind::X11, static_cast<std::uintptr_t>(glfwGetX11Window(m_handle)) };
    }
}
