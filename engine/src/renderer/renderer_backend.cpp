#include "crimson/renderer/renderer_backend.hpp"

#include "../platform/opengl/opengl_backend.hpp"
#include "crimson/renderer/renderer_api.hpp"

namespace crimson
{
     std::unique_ptr<RendererBackend> RendererBackend::Create()
     {
        switch (RendererAPI::GetType())
        {
            case RendererAPIType::OpenGL:
                return std::make_unique<OpenGLBackend>();
            default:
                return nullptr;
        }
     }
}