#pragma once
#include "crimson/renderer/renderer_backend.hpp"

namespace crimson
{
    class OpenGLBackend : public RendererBackend
    {
    public:
        void Initialize(void* windowHandle) override;
        void Execute(RenderCommandQueue& commandQueue, void* windowHandle) override;
        void Present(void* windowHandle) override;
    };
}
