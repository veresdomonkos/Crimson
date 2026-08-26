#pragma once

#include <span>

#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/core/window.hpp"
#include "shader_property_info.hpp"

namespace crimson
{
    class ResourceManager
    {
    public:
        virtual ~ResourceManager() = default;

        virtual RenderSurfaceHandle CreateRenderSurface(const Window& window) = 0;
        [[nodiscard]] virtual RenderTargetHandle GetCurrentBackBuffer(RenderSurfaceHandle renderSurface) const = 0;

        virtual VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) = 0;
        virtual void DestroyVertexBuffer(VertexBufferHandle handle) = 0;

        virtual IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) = 0;
        virtual void DestroyIndexBuffer(IndexBufferHandle handle) = 0;

        virtual ShaderHandle CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary) = 0;
        virtual void DestroyShader(ShaderHandle handle) = 0;

        virtual MaterialHandle CreateMaterial(ShaderHandle shaderHandle) = 0;
        virtual void DestroyMaterial(MaterialHandle handle) = 0;

        template<MaterialProperty T>
        void SetMaterialPropertyByName(MaterialHandle handle, std::string_view name, const T& value)
        {
            const auto bytes = std::span(
                reinterpret_cast<const std::byte*>(&value),
                sizeof(T)
            );
            SetMaterialPropertyByNameImpl(handle, name, bytes);
        }
    protected:
        virtual void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) = 0;
    };
}
