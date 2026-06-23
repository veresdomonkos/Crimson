#pragma once

#include "crimson/renderer/resource_handles.hpp"
#include "crimson/core/window.hpp"

namespace crimson
{
    class ResourceManager
    {
    public:
        virtual ~ResourceManager() = default;
        virtual RenderSurfaceHandle CreateRenderSurface(const Window& window) = 0;
        virtual RenderTargetHandle GetBackBuffer(RenderSurfaceHandle renderSurface) = 0;
    };
}
