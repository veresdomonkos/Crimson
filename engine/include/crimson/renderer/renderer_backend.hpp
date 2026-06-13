#pragma once

#include <memory>
#include "crimson/renderer/render_command_queue.hpp"

namespace crimson
{
    class RendererBackend
    {
    public:
        virtual ~RendererBackend() = default;
        virtual void Execute(const RenderCommandQueue& commandQueue) = 0;

        static RendererBackend* Create();
    };
}