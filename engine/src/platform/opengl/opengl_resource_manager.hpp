#pragma once
#include "opengl_resources.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/handle_registry.hpp"
#include "crimson/renderer/resource_manager.hpp"
#include "crimson/renderer/resource_manager_base.hpp"

namespace crimson::opengl
{
    class OpenGLResourceManager : public ResourceManagerBase<OpenglResourceTraits>
    {
    public:
        RenderSurfaceHandle CreateRenderSurface(const Window& window) override;
        RenderTargetHandle GetCurrentBackBuffer(RenderSurfaceHandle handle) const override { return m_renderSurfaces.Get(handle).BackBufferHandle; }

        VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) override;
        void DestroyVertexBuffer(VertexBufferHandle handle) override;

        IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) override;
        void DestroyIndexBuffer(IndexBufferHandle handle) override;

        VertexArrayHandle CreateVertexArray(const VertexArrayInfo& info);
        VertexArray& GetVertexArray(VertexArrayHandle handle) { return m_vertexArrays.Get(handle); }

        ShaderHandle CreateShader(std::string_view vertexSrc, std::string_view fragmentSrc) override;
        void DestroyShader(ShaderHandle handle) override;
    protected:
        OpenGLGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo &info) override;
    private:
        static GLuint CompileShader(GLenum type, std::string_view source);
    private:
        HandleRegistry<VertexArrayHandle, VertexArray> m_vertexArrays;
    };
}
