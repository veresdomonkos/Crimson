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

    FrameContext OpenGLRenderer::BeginFrame(RenderSurfaceHandle surfaceHandle)
    {
        auto* window = static_cast<GLFWwindow*>(m_resourceManager.GetRenderSurface(surfaceHandle).WindowHandle);
        glfwMakeContextCurrent(window);

        m_frames[0].Reset();
        m_frames[0].Init(surfaceHandle, m_resourceManager.GetCurrentBackBuffer(surfaceHandle), true);
        return m_frames[0].CreateContext();
    }

    void OpenGLRenderer::EndFrame(const FrameContext& frameContext)
    {
        Frame& frame = m_frames[frameContext.GetIndex()];
        auto* window = static_cast<GLFWwindow*>(m_resourceManager.GetRenderSurface(frame.GetSurface()).WindowHandle);
        glfwMakeContextCurrent(window);

        for (const auto& renderPass : frame.GetRenderPasses())
        {
            ExecuteBeginRenderPass(renderPass.Info());
            for (const auto& draw : renderPass.GetDraws())
            {
                // Draw
            }
        }

        glfwSwapBuffers(window);
    }

    void OpenGLRenderer::ExecuteBeginRenderPass(const RenderPassInfo& info)
    {
        const auto& target = m_resourceManager.GetRenderTarget(info.Target);

        glBindFramebuffer(GL_FRAMEBUFFER, target.FrameBufferHandle);
        glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));

        GLbitfield clearMask = 0;

        if (HasClearFlag(info.ClearFlags, ClearFlags::Color))
        {
            glClearColor(info.ClearColor.r, info.ClearColor.g, info.ClearColor.b, info.ClearColor.a);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }

        if (HasClearFlag(info.ClearFlags, ClearFlags::Depth))
        {
            glClearDepth(info.ClearDepth);
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }

        if (HasClearFlag(info.ClearFlags, ClearFlags::Stencil))
        {
            glClearStencil(static_cast<GLint>(info.ClearStencil));
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }

        if (clearMask != 0)
            glClear(clearMask);
    }
}
