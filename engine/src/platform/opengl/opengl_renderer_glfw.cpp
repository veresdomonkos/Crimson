#include "opengl_renderer.hpp"
#include "crimson/core/log.hpp"
#include "glad/glad.h"
#include "GLFW/glfw3.h"

namespace crimson::opengl
{
    RenderSurfaceHandle OpenGLRenderer::Initialize(const Window& primaryWindow)
    {
        auto* window = static_cast<GLFWwindow*>(primaryWindow.GetNativeHandle());
        glfwMakeContextCurrent(window);

        const RenderSurfaceHandle surfaceHandle = m_resourceManager.CreateRenderSurface(primaryWindow);

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

    std::optional<FrameContext> OpenGLRenderer::BeginFrame(RenderSurfaceHandle surfaceHandle)
    {
        auto* window = static_cast<GLFWwindow*>(m_resourceManager.GetRenderSurface(surfaceHandle).WindowHandle);
        glfwMakeContextCurrent(window);

        m_commandBuffer.Clear();
        return FrameContext(surfaceHandle, m_resourceManager.GetCurrentBackBuffer(surfaceHandle), m_commandBuffer);
    }

    void OpenGLRenderer::EndFrame(FrameContext& frame)
    {
        auto* window = static_cast<GLFWwindow*>(m_resourceManager.GetRenderSurface(frame.GetSurfaceHandle()).WindowHandle);
        glfwMakeContextCurrent(window);

        for (const auto& commandView : frame.GetCommandBuffer())
        {
            switch (commandView.GetType())
            {
                case RendererCommandType::Clear:
                {
                    const auto& cmd = commandView.As<ClearCommand>();
                    glClearColor(cmd.Color.r, cmd.Color.g, cmd.Color.b, cmd.Color.a);
                    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
                    break;
                }
                case RendererCommandType::BeginRenderPass:
                {
                    const auto& cmd = commandView.As<BeginRenderPassCommand>();
                    ExecuteBeginRenderPass(cmd);
                    break;
                }
                default:
                    break;
            }
        }

        glfwSwapBuffers(window);
    }

    void OpenGLRenderer::ExecuteBeginRenderPass(const BeginRenderPassCommand& cmd)
    {
        const auto& target = m_resourceManager.GetRenderTarget(cmd.Target);

        glBindFramebuffer(GL_FRAMEBUFFER, target.FrameBufferHandle);
        glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));

        GLbitfield clearMask = 0;

        if (HasClearFlag(cmd.ClearFlags, ClearFlags::Color))
        {
            glClearColor(cmd.ClearColor.r, cmd.ClearColor.g, cmd.ClearColor.b, cmd.ClearColor.a);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }

        if (HasClearFlag(cmd.ClearFlags, ClearFlags::Depth))
        {
            glClearDepth(cmd.ClearDepth);
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }

        if (HasClearFlag(cmd.ClearFlags, ClearFlags::Stencil))
        {
            glClearStencil(static_cast<GLint>(cmd.ClearStencil));
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }

        if (clearMask != 0)
            glClear(clearMask);
    }
}
