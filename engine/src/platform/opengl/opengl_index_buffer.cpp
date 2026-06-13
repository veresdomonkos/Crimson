#include "opengl_index_buffer.hpp"

#include "crimson/renderer/buffers/index_buffer.hpp"
#include "glad/glad.h"

namespace crimson::opengl
{
    OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t *indices, uint32_t count) : m_count(count)
    {
        glCreateBuffers(1, &m_ebo);
        glNamedBufferData(m_ebo, static_cast<GLsizeiptr>(count * sizeof(uint32_t)), indices, GL_STATIC_DRAW);
    }

    OpenGLIndexBuffer::~OpenGLIndexBuffer()
    {
        glDeleteBuffers(1, &m_ebo);
    }
}
