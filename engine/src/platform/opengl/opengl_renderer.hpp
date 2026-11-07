#pragma once
#include "opengl_resource_manager.hpp"
#include "opengl_device.hpp"
#include "opengl_state_cache.hpp"
#include "crimson/renderer/draw_sort.hpp"
#include "crimson/renderer/renderer.hpp"

#include <vector>

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
        void ExecuteBeginRawPass(const RawPassInfo& info);
        void ClearTarget(ClearFlags flags, const glm::vec4& color, float depth, uint32_t stencil);
        void ResetBoundState();
        void BindFrameResources();
    private:
        GLuint m_frameUBO = 0;
        GLuint m_viewUBO = 0;
        GLuint m_lightsBuffer = 0;
        GLuint m_shadowsBuffer = 0;
        GLuint m_objectsBuffer = 0;
        GLuint m_shadowAtlas = 0;

        OpenGLDevice& m_device;
        OpenGLResourceManager& m_resourceManager;
        Frame m_frames[1];

        std::vector<DrawSortEntry> m_drawOrder;
        OpenGLStateCache m_state;
        MaterialHandle m_boundMaterial;
        ShaderPass m_boundPass = ShaderPass::Main;
        VertexBufferHandle m_boundVertexBuffer;
        IndexBufferHandle m_boundIndexBuffer;
        ObjectId m_boundObject = 0;
    };
}
