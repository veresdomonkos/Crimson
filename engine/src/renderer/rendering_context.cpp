#include "crimson/renderer/rendering_context.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
    namespace opengl
    {
        std::unique_ptr<RenderingContext> CreateOpenGLContext(void* windowHandle);
    }

    std::unique_ptr<RenderingContext> RenderingContext::Create(void *windowHandle)
    {
        switch (Renderer::API())
        {
            case RendererAPI::OpenGL:
                return opengl::CreateOpenGLContext(windowHandle);
            default:
                return nullptr;
        }
    }
}
