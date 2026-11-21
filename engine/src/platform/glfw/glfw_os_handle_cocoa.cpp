// The Cocoa window of a GLFW window. A file per platform, as glfw3native.h needs the platform chosen before it is included.

#include "glfw_window.hpp"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace crimson::glfw
{
    OSWindowHandle GLFWWindow::GetOSHandle() const
    {
        return { OSWindowHandle::Kind::Cocoa, reinterpret_cast<std::uintptr_t>(glfwGetCocoaWindow(m_handle)) };
    }
}
