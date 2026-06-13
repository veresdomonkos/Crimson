#include  "crimson/renderer/buffers/vertex_buffer.hpp"

#include "../../platform/opengl/opengl_vertex_buffer.hpp"
#include  "crimson/renderer/renderer.hpp"

namespace crimson
{
    std::shared_ptr<VertexBuffer> VertexBuffer::Create(float *vertices, size_t size)
    {
        switch (Renderer::API())
        {
            case RendererAPI::OpenGL: return std::make_shared<opengl::OpenGLVertexBuffer>(vertices, size);
            default: return nullptr;
        }
    }
}
