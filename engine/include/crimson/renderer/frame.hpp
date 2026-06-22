#pragma once
#include "render_command_queue.hpp"
#include "render_surface.hpp"
#include "glm/vec4.hpp"

namespace crimson
{
    class Frame
    {
    public:
        Frame(SurfaceHandle surfaceHandle, size_t commandBufferSize) : m_surfaceHandle(surfaceHandle), m_commandQueue(commandBufferSize) {}

        void Clear(const glm::vec4& color)
        {
            m_commandQueue.Submit<ClearCommand>(color);
        }

        RenderCommandQueue& GetCommandQueue() { return m_commandQueue; }
        SurfaceHandle GetSurfaceHandle() const { return m_surfaceHandle; }
    protected:
        SurfaceHandle m_surfaceHandle;
        RenderCommandQueue m_commandQueue;
    };
}
