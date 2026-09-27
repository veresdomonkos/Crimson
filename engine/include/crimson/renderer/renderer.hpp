#pragma once
#include "frame_context.hpp"
#include "frame_data.hpp"
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
        virtual const ResourceManager& GetResourceManager() const = 0;

        FrameContext BeginFrame(const FrameLightingData& lighting) { return BeginFrame(RenderSurfaceHandle(0, 1), lighting); }
        virtual FrameContext BeginFrame(RenderSurfaceHandle surfaceHandle, const FrameLightingData& lighting) = 0;
        virtual void EndFrame(const FrameContext& frameContext) = 0;

        virtual void SetShadowMap(TextureHandle shadowMap) = 0;

        static Unique<Renderer> Create();
    };
}
