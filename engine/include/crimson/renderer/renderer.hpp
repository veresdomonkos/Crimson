#pragma once
#include "frame.hpp"
#include "resource_manager.hpp"

namespace crimson
{
    class Renderer
    {
    public:
        virtual ~Renderer() = default;
        virtual SurfaceHandle Initialize(const Window& primaryWindow) = 0;
        virtual void Shutdown() = 0;
        virtual ResourceManager& GetResourceManager() = 0;
        virtual Frame BeginFrame(SurfaceHandle surface) = 0;
        virtual void EndFrame(Frame& frame) = 0;
        static Unique<Renderer> Create();
    };
}
