#pragma once
#include "opengl_resource_manager.hpp"
#include "crimson/renderer/camera_data.hpp"
#include "crimson/renderer/renderer.hpp"


namespace crimson::opengl
{
    class OpenGLRenderer : public Renderer
    {
    public:
        RenderSurfaceHandle Initialize(const Window& primaryWindow) override;
        void Shutdown() override;
        ResourceManager& GetResourceManager() override { return  m_resourceManager; }
        const ResourceManager& GetResourceManager() const override { return m_resourceManager; }
        FrameContext BeginFrame(RenderSurfaceHandle surfaceHandle, const FrameLightingData& lighting) override;
        void EndFrame(const FrameContext& frameContext) override;
        void SetShadowMap(TextureHandle shadowMap) override;
    private:
        void ExecuteDraw(const DrawInfo &info);
        void ExecuteBeginRenderPass(const RenderPassInfo& info);
    private:
        GLuint m_cameraUBO = 0;
        GLuint m_lightingUBO = 0;

        OpenGLResourceManager m_resourceManager;
        Frame m_frames[1];
    };
}
