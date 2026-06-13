#include "opengl_context.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "crimson/core/log.hpp"

namespace crimson::opengl
{
    std::unique_ptr<RenderingContext> CreateOpenGLContext(void* windowHandle)
    {
        return std::make_unique<OpenGLContext>(windowHandle);
    }

    OpenGLContext::OpenGLContext(void* windowHandle) : m_windowHandle(windowHandle)
    {
        static bool s_gladInitialized = false;

        if (s_gladInitialized)
            return;

        glfwMakeContextCurrent(static_cast<GLFWwindow*>(windowHandle));

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            LOG_ERROR("[Renderer] Failed to initialize GLAD!");
            return;
        }

        LOG_WARN("[Renderer] GLAD Initialized successfully!");
        LOG_INFO("[Renderer] OpenGL Version: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        LOG_INFO("[Renderer] GPU Vendor:     {}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        LOG_INFO("[Renderer] Renderer:       {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

        s_gladInitialized = true;
    }

    void OpenGLContext::SwapBuffers()
    {
        glfwSwapBuffers(static_cast<GLFWwindow*>(m_windowHandle));
    }

    void OpenGLContext::MakeCurrent()
    {
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_windowHandle));
    }
}
