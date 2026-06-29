#pragma once
#include "frame_context.hpp"
#include "resource_manager.hpp"

namespace crimson
{
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual RenderSurfaceHandle Initialize(const Window& primaryWindow) = 0;
        virtual void Shutdown() = 0;

        virtual ResourceManager& GetResourceManager() = 0;

        virtual FrameContext BeginFrame(RenderSurfaceHandle surface) = 0;
        virtual void EndFrame(const FrameContext& frameContext) = 0;

        static Unique<Renderer> Create();
    };
}
