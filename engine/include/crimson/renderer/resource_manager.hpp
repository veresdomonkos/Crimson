#pragma once

#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/core/window.hpp"

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
    };
}
