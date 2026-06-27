#pragma once
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
        RenderSurfaceHandle CreateRenderSurface(const Window& window) override;
        RenderTargetHandle GetCurrentBackBuffer(RenderSurfaceHandle handle) const override { return m_renderSurfaces.Get(handle).BackBufferHandle; }
    };
}
