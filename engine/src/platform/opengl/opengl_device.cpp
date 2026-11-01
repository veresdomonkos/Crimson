#include "opengl_device.hpp"

#include <cstdlib>

#include "crimson/core/log.hpp"
#include <glad/glad.h>

namespace crimson::opengl
{
    OpenGLDevice::OpenGLDevice(const Window &window)
        : m_primaryWindow(static_cast<GLFWwindow*>(window.GetNativeHandle()))
    {
        glfwMakeContextCurrent(m_primaryWindow);
        glfwSwapInterval(std::getenv("CRIMSON_VSYNC") ? 1 : 0);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            LOG_ERROR("[Renderer] Failed to initialize GLAD!");
        }

        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    }
}
