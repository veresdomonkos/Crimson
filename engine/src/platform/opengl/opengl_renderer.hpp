#pragma once
#include "opengl_resource_manager.hpp"
#include "opengl_device.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson::opengl
{
    class OpenGLRenderer : public Renderer
    {
    public:
        OpenGLRenderer(OpenGLDevice& device, OpenGLResourceManager& resourceManager);
        ~OpenGLRenderer() override;

        FrameContext BeginFrame(const FrameLightingData& lighting) override;
        void EndFrame(const FrameContext& frameContext) override;
        void SetShadowMap(TextureHandle shadowMap) override;
    private:
        void ExecuteDraw(const DrawInfo &info);
        void ExecuteBeginRenderPass(const RenderPassInfo& info);
    private:
        GLuint m_cameraUBO = 0;
        GLuint m_lightingUBO = 0;

        OpenGLDevice& m_device;
        OpenGLResourceManager& m_resourceManager;
        Frame m_frames[1];
    };
}
