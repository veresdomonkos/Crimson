
#include "crimson/renderer/buffers/index_buffer.hpp"

#include "../../platform/opengl/opengl_index_buffer.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
    std::shared_ptr<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        switch (Renderer::API())
        {
            case RendererAPI::OpenGL: return std::make_shared<opengl::OpenGLIndexBuffer>(indices, count);
            default: return nullptr;
        }
    }
}
