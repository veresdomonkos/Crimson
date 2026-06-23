#pragma once
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/render_commands.hpp"
#include "crimson/core/command_buffer.hpp"
#include "glm/vec4.hpp"

namespace crimson
{
    class Frame
    {
    public:
        Frame(RenderSurfaceHandle surfaceHandle, size_t commandBufferSize) : m_surfaceHandle(surfaceHandle), m_commandBuffer(commandBufferSize) {}

        void BeginRenderPass(const RenderPassInfo& info)
        {
            m_commandBuffer.Submit<BeginRenderPassCommand>(info);
        }

        void EndRenderPass()
        {

        }

        RenderCommandBuffer& GetCommandBuffer() { return m_commandBuffer; }
        RenderSurfaceHandle GetSurfaceHandle() const { return m_surfaceHandle; }
    protected:
        RenderSurfaceHandle m_surfaceHandle;
        RenderCommandBuffer m_commandBuffer;
    };
}
