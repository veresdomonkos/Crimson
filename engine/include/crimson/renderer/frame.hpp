#pragma once
#include "render_commands.hpp"
#include "render_surface.hpp"
#include "crimson/core/command_buffer.hpp"
#include "glm/vec4.hpp"

namespace crimson
{
    class Frame
    {
    public:
        Frame(SurfaceHandle surfaceHandle, size_t commandBufferSize) : m_surfaceHandle(surfaceHandle), m_commandBuffer(commandBufferSize) {}

        void Clear(const glm::vec4& color)
        {
            m_commandBuffer.Submit<ClearCommand>(color);
        }

        RenderCommandBuffer& GetCommandBuffer() { return m_commandBuffer; }
        SurfaceHandle GetSurfaceHandle() const { return m_surfaceHandle; }
    protected:
        SurfaceHandle m_surfaceHandle;
        RenderCommandBuffer m_commandBuffer;
    };
}
