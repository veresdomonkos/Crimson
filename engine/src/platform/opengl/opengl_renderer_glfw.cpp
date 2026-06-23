#include "opengl_renderer.hpp"
#include "crimson/core/log.hpp"
#include "glad/glad.h"
#include "GLFW/glfw3.h"

namespace crimson::opengl
{
    SurfaceHandle OpenGLRenderer::Initialize(const Window& primaryWindow)
    {
        auto* window = static_cast<GLFWwindow*>(primaryWindow.GetNativeHandle());
        glfwMakeContextCurrent(window);

        const SurfaceHandle surfaceHandle = m_resourceManager.CreateSurface(primaryWindow);

        static bool s_gladInitialized = false;

        if (s_gladInitialized)
            return surfaceHandle;

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            LOG_ERROR("[Renderer] Failed to initialize GLAD!");
            return surfaceHandle;
        }

        LOG_WARN("[Renderer] GLAD Initialized successfully!");
        LOG_INFO("[Renderer] OpenGL Version: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        LOG_INFO("[Renderer] GPU Vendor:     {}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        LOG_INFO("[Renderer] Renderer:       {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

        s_gladInitialized = true;

        return surfaceHandle;
    }

    void OpenGLRenderer::Shutdown()
    {

    }

    Frame OpenGLRenderer::BeginFrame(SurfaceHandle surfaceHandle)
    {
        auto* window = static_cast<GLFWwindow*>(m_resourceManager.GetSurface(surfaceHandle));
        glfwMakeContextCurrent(window);

        return Frame(surfaceHandle, 2 * 1024 * 1024);
    }

    void OpenGLRenderer::EndFrame(Frame& frame)
    {
        auto* window = static_cast<GLFWwindow*>(m_resourceManager.GetSurface(frame.GetSurfaceHandle()));
        glfwMakeContextCurrent(window);

        for (const auto& commandView : frame.GetCommandBuffer())
        {
            switch (commandView.GetType())
            {
                case RendererCommandType::Clear:
                {
                    const auto& command = commandView.As<ClearCommand>();
                    glClearColor(command.Color.r, command.Color.g, command.Color.b, command.Color.a);
                    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
                    break;
                }
                default:
                    break;
            }
        }

        glfwSwapBuffers(window);
    }
}
