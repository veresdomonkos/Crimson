#pragma once
#include "crimson/renderer/resource_manager.hpp"

namespace crimson::opengl
{
    class OpenGLResourceManager : public ResourceManager
    {
    public:
        SurfaceHandle CreateSurface(const Window& window) override;
        void* GetSurface(SurfaceHandle surfaceHandle) { return m_renderSurfaces[surfaceHandle]; }
    private:
        std::unordered_map<SurfaceHandle, void*> m_renderSurfaces;
    };
}