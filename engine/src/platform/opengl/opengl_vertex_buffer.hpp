#pragma once
#include "crimson/renderer/buffers/vertex_buffer.hpp"

namespace crimson::opengl
{
    class OpenGLVertexBuffer : public VertexBuffer
    {
    public:
        OpenGLVertexBuffer(float* vertices, size_t size);
        ~OpenGLVertexBuffer() override;

        void SetLayout(const BufferLayout& layout) override;
        const BufferLayout& GetLayout() const override;
        ResourceHandle GetHandle() const override;
    private:
        uint32_t m_vbo = 0;
        uint32_t m_vao = 0;
        BufferLayout m_Layout;
    };
}
