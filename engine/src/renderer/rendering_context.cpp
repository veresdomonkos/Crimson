#include "crimson/renderer/rendering_context.hpp"

#include "../platform/opengl/opengl_context.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
    std::unique_ptr<RenderingContext> RenderingContext::Create(void *windowHandle)
    {
        switch (Renderer::API())
        {
            case RendererAPI::OpenGL:
                return std::make_unique<opengl::OpenGLContext>(windowHandle);
            default:
                return nullptr;
        }
    }
}
