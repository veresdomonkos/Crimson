#pragma once
#include "opengl_resource_manager.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson::opengl
{
    class OpenGLRenderer : public Renderer
    {
    public:
        SurfaceHandle Initialize(const Window& primaryWindow) override;
        ResourceManager& GetResourceManager() override { return  m_resourceManager; }
        Frame BeginFrame(SurfaceHandle surfaceHandle) override;
        void EndFrame(Frame& frame) override;
    private:
        OpenGLResourceManager m_resourceManager;

    };
}
