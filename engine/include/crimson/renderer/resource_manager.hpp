#pragma once

#include "render_surface.hpp"
#include "crimson/core/window.hpp"

namespace crimson
{
    class ResourceManager
    {
    public:
        virtual ~ResourceManager() = default;
        virtual SurfaceHandle CreateSurface(const Window& window) = 0;
    };
}
