#pragma once
#include "crimson/renderer/render_core.hpp"
#include "crimson/renderer/buffers/index_buffer.hpp"

namespace crimson::opengl
{
    class OpenGLIndexBuffer : public IndexBuffer
    {
    public:
        OpenGLIndexBuffer(uint32_t* indices, uint32_t count);
        ~OpenGLIndexBuffer() override;

        uint32_t GetCount() const override { return m_count; }
        ResourceHandle GetHandle() const override { return m_ebo; }

    private:
        uint32_t m_ebo = 0;
        uint32_t m_count = 0;
    };
}
