#pragma once
#include "opengl_resource_manager.hpp"
#include "opengl_device.hpp"
#include "opengl_state_cache.hpp"
#include "crimson/renderer/draw_sort.hpp"
#include "crimson/renderer/frame_validation.hpp"
#include "crimson/renderer/renderer.hpp"

#include <vector>

namespace crimson::opengl
{
    class OpenGLRenderer : public Renderer
    {
    public:
        OpenGLRenderer(OpenGLDevice& device, OpenGLResourceManager& resourceManager);
        ~OpenGLRenderer() override;

        [[nodiscard]] bool CanRender() const override;
        bool Submit(FrameRecorder frame) override;
        void SetShadowAtlas(TextureHandle shadowAtlas) override;
    private:
        void ExecutePass(const PassRecord& pass);
        void BeginPass(const PassInfo& info);
        void ExecuteNativePass(const PassRecord& pass);
        void BindTarget(const PassInfo& info);
        void ClearTarget(const ClearInfo& clear);
        void ExecuteDraw(const DrawItem& info);
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

        std::vector<DrawSortEntry> m_drawOrder;
        FrameValidator m_validator;
        OpenGLStateCache m_state;
        MaterialHandle m_boundMaterial;
        ShaderVariant m_boundVariant = ShaderVariant::Main;
        VertexBufferHandle m_boundVertexBuffer;
        IndexBufferHandle m_boundIndexBuffer;
        ObjectId m_boundObject = 0;
    };
}
