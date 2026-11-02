#pragma once
#include <string_view>
#include "opengl_device.hpp"
#include "opengl_resources.hpp"
#include "vertex_array_cache.hpp"
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

        GLuint GetOrCreateVertexArray(VertexBufferHandle vertexBuffer, IndexBufferHandle indexBuffer);

        ShaderHandle CreateShader(const ShaderBinary& binary) override;
        void DestroyShader(ShaderHandle handle) override;

        MaterialHandle CreateMaterial(ShaderHandle shaderHandle, const MaterialSettings& settings) override;
        void DestroyMaterial(MaterialHandle handle) override;
        void SetMaterialTexture(MaterialHandle material, std::string_view name, TextureHandle texture) override;

        TextureHandle CreateTexture(const TextureInfo& info, const void* data) override;
        void DestroyTexture(TextureHandle handle) override;
        std::vector<std::byte> ReadColorPixels(TextureHandle texture) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        [[nodiscard]] TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const override;
        [[nodiscard]] std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const override;

        [[nodiscard]] RenderTargetHandle GetBackBufferHandle() const { return m_backBufferHandle; }
        [[nodiscard]] GLuint GetDefaultTexture() const { return m_textures.Get(m_defaultTexture).GLHandle; }
    protected:
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override;
    private:
        static GLuint CompileShader(GLenum type, std::string_view source);
        [[nodiscard]] GLuint GetSharedSampler(std::string_view name) const;
    private:
        VertexArrayCache m_vertexArrays;
        RenderTargetHandle m_backBufferHandle;
        TextureHandle m_defaultTexture; // what a material's textures show until they are set

        GLuint m_linearWrapSampler = 0;
        GLuint m_linearClampSampler = 0;
        GLuint m_pointClampSampler = 0;
        GLuint m_shadowCompareSampler = 0;
    };
}
