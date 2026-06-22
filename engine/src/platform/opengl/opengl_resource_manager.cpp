#include "opengl_resource_manager.hpp"

namespace crimson::opengl
{
    SurfaceHandle OpenGLResourceManager::CreateSurface(const Window &window)
    {
        static SurfaceHandle id = 0;
        m_renderSurfaces[++id] = window.GetNativeHandle();
        return id;
    }
}
