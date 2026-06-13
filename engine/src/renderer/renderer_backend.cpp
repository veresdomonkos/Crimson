#include "crimson/renderer/renderer_backend.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
     namespace opengl
     {
         RendererBackend* CreateOpenGLBackend();
     }

     RendererBackend* RendererBackend::Create()
     {
        switch (Renderer::API())
        {
            case RendererAPI::OpenGL:
                return opengl::CreateOpenGLBackend();
            default:
                return nullptr;
        }
     }
}