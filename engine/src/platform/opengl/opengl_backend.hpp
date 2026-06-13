#pragma once
#include "crimson/renderer/renderer_backend.hpp"

namespace crimson::opengl
{
    class OpenGLBackend : public RendererBackend
    {
    public:
        OpenGLBackend();
        void Execute(const RenderCommandQueue& commandQueue) override;
    };
}
