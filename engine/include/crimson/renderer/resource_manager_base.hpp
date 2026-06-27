#pragma once
#include "handle_registry.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    template <typename PlatformResourceTraits>
    class ResourceManagerBase : public ResourceManager
    {
    private:
        using RenderSurface = PlatformResourceTraits::RenderSurface;
        using RenderTarget = PlatformResourceTraits::RenderTarget;
    public:
        RenderSurface& GetRenderSurface(RenderSurfaceHandle handle) { return m_renderSurfaces.Get(handle); }
        RenderTarget& GetRenderTarget(RenderTargetHandle handle) { return m_renderTargets.Get(handle); }
    protected:
        HandleRegistry<RenderSurfaceHandle, RenderSurface> m_renderSurfaces;
        HandleRegistry<RenderTargetHandle, RenderTarget> m_renderTargets;
    };
}
