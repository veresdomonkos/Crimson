#pragma once

#include "crimson/renderer/handle_registry.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    template <typename PlatformResourceTraits>
    class ResourceManagerBase : public GpuResourceManager
    {
    public:
        using RenderSurface = PlatformResourceTraits::RenderSurface;
        using RenderTarget = PlatformResourceTraits::RenderTarget;
        using VertexBuffer = PlatformResourceTraits::VertexBuffer;
        using IndexBuffer = PlatformResourceTraits::IndexBuffer;
        using Shader = PlatformResourceTraits::Shader;
        using Material = PlatformResourceTraits::Material;
        using Texture = PlatformResourceTraits::Texture;
    public:
        RenderTarget& GetRenderTarget(RenderTargetHandle handle) { return m_renderTargets.Get(handle); }
        VertexBuffer& GetVertexBuffer(VertexBufferHandle handle) { return m_vertexBuffers.Get(handle); }
        IndexBuffer& GetIndexBuffer(IndexBufferHandle handle) { return m_indexBuffers.Get(handle); }
        Shader& GetShader(ShaderHandle handle) { return m_shaders.Get(handle); }
        Material& GetMaterial(MaterialHandle handle) { return  m_materials.Get(handle); }
        Texture& GetTexture(TextureHandle handle) { return m_textures.Get(handle); }

        const RenderTarget& GetRenderTarget(RenderTargetHandle handle) const { return m_renderTargets.Get(handle); }
        const VertexBuffer& GetVertexBuffer(VertexBufferHandle handle) const { return m_vertexBuffers.Get(handle); }
        const IndexBuffer& GetIndexBuffer(IndexBufferHandle handle) const { return m_indexBuffers.Get(handle); }
        const Shader& GetShader(ShaderHandle handle) const { return m_shaders.Get(handle); }
        const Material& GetMaterial(MaterialHandle handle) const { return  m_materials.Get(handle); }
        const Texture& GetTexture(TextureHandle handle) const { return m_textures.Get(handle); }
    protected:
        HandleRegistry<RenderTargetHandle, RenderTarget> m_renderTargets;
        HandleRegistry<VertexBufferHandle, VertexBuffer> m_vertexBuffers;
        HandleRegistry<IndexBufferHandle, IndexBuffer> m_indexBuffers;
        HandleRegistry<ShaderHandle, Shader> m_shaders;
        HandleRegistry<MaterialHandle, Material> m_materials;
        HandleRegistry<TextureHandle, Texture> m_textures;
    };
}
