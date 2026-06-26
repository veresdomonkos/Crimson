#pragma once
#include "opengl_resource_manager.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson::opengl
{
    class OpenGLRenderer : public Renderer
    {
    public:
        OpenGLRenderer() : m_commandBuffer(2 * 1024 * 1024) {}
        RenderSurfaceHandle Initialize(const Window& primaryWindow) override;
        void Shutdown() override;
        ResourceManager& GetResourceManager() override { return  m_resourceManager; }
        std::optional<Frame> BeginFrame(RenderSurfaceHandle surfaceHandle) override;
        void EndFrame(Frame& frame) override;
    private:
        void ExecuteBeginRenderPass(const BeginRenderPassCommand& cmd);
    private:
        OpenGLResourceManager m_resourceManager;
        RenderCommandBuffer m_commandBuffer;
    };
}
