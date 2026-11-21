#include "opengl_device.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "crimson/core/log.hpp"
#include <glad/glad.h>

namespace crimson::opengl
{
    OpenGLDevice::OpenGLDevice(WindowGraphics& windowGraphics)
        : m_windowGraphics(windowGraphics)
    {
        m_windowGraphics.MakeContextCurrent();
        m_windowGraphics.SetSwapInterval(std::getenv("CRIMSON_VSYNC") ? 1 : 0);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(m_windowGraphics.GetGLLoader())))
        {
            throw std::runtime_error("Failed to load OpenGL.");
        }

        const auto describe = [](GLenum name)
        {
            const auto* text = reinterpret_cast<const char*>(glGetString(name));
            return std::string(text ? text : "unknown");
        };

        LOG_INFO("[OpenGL Device] {}, {}", describe(GL_RENDERER), describe(GL_VERSION));

        if (GLVersion.major < 4 || (GLVersion.major == 4 && GLVersion.minor < 5))
            throw std::runtime_error("OpenGL 4.5 is required, the driver has " + describe(GL_VERSION));

        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    }
}
