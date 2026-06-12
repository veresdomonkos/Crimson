#pragma once

#include <memory>
#include "crimson/renderer/render_command_queue.hpp"

namespace crimson
{
    class RendererBackend
    {
    public:
        virtual ~RendererBackend() = default;
        virtual void Execute(RenderCommandQueue& commandQueue, void* windowHandle) = 0;
        virtual void Present(void* windowHandle) = 0;
        virtual void Initialize(void* windowHandle) = 0;

        static std::unique_ptr<RendererBackend> Create();
    };
}