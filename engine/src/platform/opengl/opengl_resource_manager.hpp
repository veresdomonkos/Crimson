#pragma once
#include "opengl_device.hpp"
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
        OpenGLResourceManager(Window& window);

        VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) override;
        void DestroyVertexBuffer(VertexBufferHandle handle) override;

        IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) override;
        void DestroyIndexBuffer(IndexBufferHandle handle) override;

        VertexArrayHandle CreateVertexArray(const VertexArrayInfo& info);
        VertexArray& GetVertexArray(VertexArrayHandle handle) { return m_vertexArrays.Get(handle); }

        ShaderHandle CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary) override;
        void DestroyShader(ShaderHandle handle) override;

        MaterialHandle CreateMaterial(ShaderHandle shaderHandle) override;
        void DestroyMaterial(MaterialHandle handle) override;
        void SetMaterialTexture(MaterialHandle material, std::string_view name, TextureHandle texture) override;

        TextureHandle CreateTexture(const TextureInfo& info, const void* data) override;
        void DestroyTexture(TextureHandle handle) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        [[nodiscard]] TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const override;
        [[nodiscard]] std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const override;

        [[nodiscard]] RenderTargetHandle GetBackBufferHandle() const { return m_backBufferHandle; }
    protected:
        OpenGLGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo &info) override;
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override;
    private:
        static void ReflectShader(Shader &shader, std::span<const uint32_t> spirvCode);
        static GLuint CompileShader(GLenum type, std::string_view source);
        static GLuint CompileSPIRVShader(GLenum type, std::span<const uint32_t> binary, std::string_view stageName);
    private:
        HandleRegistry<VertexArrayHandle, VertexArray> m_vertexArrays;
        RenderTargetHandle m_backBufferHandle;
    };
}
