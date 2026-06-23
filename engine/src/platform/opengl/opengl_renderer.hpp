#pragma once
#include "opengl_resource_manager.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson::opengl
{
    class OpenGLRenderer : public Renderer
    {
    public:
        RenderSurfaceHandle Initialize(const Window& primaryWindow) override;
        void Shutdown() override;
        ResourceManager& GetResourceManager() override { return  m_resourceManager; }
        Frame BeginFrame(RenderSurfaceHandle surfaceHandle) override;
        void EndFrame(Frame& frame) override;
    private:
        void ExecuteBeginRenderPass(const BeginRenderPassCommand& cmd);
    private:
        OpenGLResourceManager m_resourceManager;

    };
}
