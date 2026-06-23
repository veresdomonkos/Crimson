#pragma once
#include "opengl_resources.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/handle_registry.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson::opengl
{
    class OpenGLResourceManager : public ResourceManager
    {
    public:
        RenderSurfaceHandle CreateRenderSurface(const Window& window) override;
        RenderTargetHandle GetBackBuffer(RenderSurfaceHandle renderSurfaceHandle) override;

        OpenGLSurface& GetRenderSurface(RenderSurfaceHandle surfaceHandle) { return m_renderSurfaces.Get(surfaceHandle); }
        OpenGLRenderTarget GetRenderTarget(RenderTargetHandle handle) { return m_renderTargets.Get(handle); }
    private:
        HandleRegistry<RenderSurfaceHandle, OpenGLSurface> m_renderSurfaces;
        HandleRegistry<RenderTargetHandle, OpenGLRenderTarget> m_renderTargets;
    };
}