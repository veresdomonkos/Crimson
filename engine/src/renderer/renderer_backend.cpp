#include "crimson/renderer/renderer_backend.hpp"

#include "../platform/opengl/opengl_backend.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
     RendererBackend* RendererBackend::Create()
     {
        switch (Renderer::API())
        {
            case RendererAPI::OpenGL:
                return new OpenGLBackend;
            default:
                return nullptr;
        }
     }
}