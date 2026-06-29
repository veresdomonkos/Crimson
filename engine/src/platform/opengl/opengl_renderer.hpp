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
        FrameContext BeginFrame(RenderSurfaceHandle surfaceHandle) override;
        void EndFrame(const FrameContext& frameContext) override;
    private:
        void ExecuteDraw(const DrawInfo &info);
        void ExecuteBeginRenderPass(const RenderPassInfo& info);
    private:
        OpenGLResourceManager m_resourceManager;
        Frame m_frames[1];
    };
}
