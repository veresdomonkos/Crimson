#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/material_settings.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/core/window.hpp"
#include "crimson/renderer/shader_interface.hpp"
#include "crimson/renderer/shader_property_info.hpp"
#include "crimson/renderer/texture.hpp"

namespace crimson
{
    class GpuResourceManager
    {
    public:
        virtual ~GpuResourceManager() = default;

        virtual VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) = 0;
        virtual void DestroyVertexBuffer(VertexBufferHandle handle) = 0;

        virtual IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) = 0;
        virtual void DestroyIndexBuffer(IndexBufferHandle handle) = 0;

        virtual ShaderHandle CreateShader(const ShaderBinary& binary) = 0;
        virtual void DestroyShader(ShaderHandle handle) = 0;

        // What the shader's materials can set
        [[nodiscard]] virtual const ShaderInterface& GetShaderInterface(ShaderHandle handle) const = 0;

        virtual MaterialHandle CreateMaterial(ShaderHandle shaderHandle, const MaterialSettings& settings) = 0;
        virtual void DestroyMaterial(MaterialHandle handle) = 0;

        // An invalid handle for a material that doesn't exist
        [[nodiscard]] virtual ShaderHandle GetMaterialShader(MaterialHandle handle) const = 0;

        void SetMaterialSettings(MaterialHandle handle, const MaterialSettings& settings)
        {
            ++m_materialRevision;
            SetMaterialSettingsImpl(handle, settings);
        }

        template<MaterialProperty T>
        void SetMaterialPropertyByName(MaterialHandle handle, std::string_view name, const T& value)
        {
            const auto bytes = std::span(
                reinterpret_cast<const std::byte*>(&value),
                sizeof(T)
            );
            ++m_materialRevision;
            SetMaterialPropertyByNameImpl(handle, name, bytes);
        }

        // An invalid texture goes back to the default white one. Can't be called inside a frame.
        void SetMaterialTexture(MaterialHandle material, std::string_view name, TextureHandle texture)
        {
            ++m_materialRevision;
            SetMaterialTextureImpl(material, name, texture);
        }

        // Goes up whenever any material's settings, properties or textures change. What was drawn with a material
        // before then, like a shadow tile that is kept between frames, may look different now.
        [[nodiscard]] uint64_t GetMaterialRevision() const { return m_materialRevision; }

        virtual TextureHandle CreateTexture(const TextureInfo& info, const void* data) = 0;
        virtual void DestroyTexture(TextureHandle handle) = 0;

        // The pixels of an RGBA8 color attachment, row by row, with the first row at the bottom of clip space (y = -1).
        // It waits for the GPU, so it is for tests and tools and not for use while rendering. Can't be called inside a frame.
        [[nodiscard]] virtual std::vector<std::byte> ReadColorPixels(TextureHandle texture) = 0;

        virtual RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) = 0;
        virtual void DestroyRenderTarget(RenderTargetHandle handle) = 0;

        [[nodiscard]] virtual TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const = 0;
        [[nodiscard]] virtual std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const = 0;
    protected:
        virtual void SetMaterialSettingsImpl(MaterialHandle handle, const MaterialSettings& settings) = 0;
        virtual void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) = 0;
        virtual void SetMaterialTextureImpl(MaterialHandle material, std::string_view name, TextureHandle texture) = 0;
    private:
        uint64_t m_materialRevision = 0;
    };
}
