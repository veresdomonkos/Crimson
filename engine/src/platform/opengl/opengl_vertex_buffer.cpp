#include "opengl_vertex_buffer.hpp"

#include "crimson/renderer/buffers/vertex_buffer.hpp"
#include "glad/glad.h"

namespace crimson::opengl
{
    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4:
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4: return GL_FLOAT;
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4: return GL_INT;
            case ShaderDataType::Bool: return GL_BOOL;
            default: return 0;
        }
    }

    static uint32_t GetComponentCount(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float:  return 1;
            case ShaderDataType::Float2: return 2;
            case ShaderDataType::Float3: return 3;
            case ShaderDataType::Float4: return 4;
            case ShaderDataType::Mat3:   return 9;
            case ShaderDataType::Mat4:   return 16;
            case ShaderDataType::Int:    return 1;
            case ShaderDataType::Int2:   return 2;
            case ShaderDataType::Int3:   return 3;
            case ShaderDataType::Int4:   return 4;
            case ShaderDataType::Bool:   return 1;
            default: return 0;
        }
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, size_t size) : m_vao(0)
    {
        glCreateBuffers(1, &m_vbo);
        glNamedBufferData(m_vbo, static_cast<GLsizeiptr>(size), vertices, GL_STATIC_DRAW);
    }

    OpenGLVertexBuffer::~OpenGLVertexBuffer()
    {
        glDeleteBuffers(1, &m_vbo);
        if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
    }

    void OpenGLVertexBuffer::SetLayout(const BufferLayout& layout)
    {
        m_Layout = layout;

        if (m_vao != 0)
        {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }

        glCreateVertexArrays(1, &m_vao);
        glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, m_Layout.GetStride());

        uint32_t index = 0;
        for (const auto& element : m_Layout.GetElements())
        {
            glEnableVertexArrayAttrib(m_vao, index);
            glVertexArrayAttribFormat(
             m_vao,
             index,
             static_cast<GLint>(GetComponentCount(element.Type)),
             ShaderDataTypeToOpenGLBaseType(element.Type),
             GL_FALSE,
             static_cast<GLuint>(element.Offset)
            );

            glVertexArrayAttribBinding(m_vao, index, 0);

            index++;
        }
    }

    const BufferLayout& OpenGLVertexBuffer::GetLayout() const { return  m_Layout; }
    ResourceHandle OpenGLVertexBuffer::GetHandle() const {  return m_vao; }
}
