#include "opengl_resource_manager.hpp"

namespace crimson::opengl
{
    RenderSurfaceHandle OpenGLResourceManager::CreateRenderSurface(const Window &window)
    {
        RenderTargetHandle backBuffer = m_renderTargets.Register(OpenGLRenderTarget{.Width = window.Width(), .Height = window.Height(), .FrameBufferHandle = 0});
        return  m_renderSurfaces.Register(OpenGLSurface{.WindowHandle = window.GetNativeHandle(), .BackBufferHandle = backBuffer});
    }

    RenderTargetHandle OpenGLResourceManager::GetBackBuffer(RenderSurfaceHandle renderSurfaceHandle)
    {
        return m_renderSurfaces.Get(renderSurfaceHandle).BackBufferHandle;
    }
}
