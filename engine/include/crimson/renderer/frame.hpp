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
        Frame(RenderSurfaceHandle surfaceHandle, RenderCommandBuffer& commandBuffer) : m_surfaceHandle(surfaceHandle), m_commandBuffer(commandBuffer) {}

        void BeginRenderPass(const RenderPassInfo& info)
        {
            m_commandBuffer.Submit<BeginRenderPassCommand>(info);
        }

        void EndRenderPass()
        {
            m_commandBuffer.Submit<EndRenderPassCommand>();
        }

        RenderCommandBuffer& GetCommandBuffer() { return m_commandBuffer; }
        RenderSurfaceHandle GetSurfaceHandle() const { return m_surfaceHandle; }
    protected:
        RenderSurfaceHandle m_surfaceHandle;
        RenderCommandBuffer& m_commandBuffer;
    };
}
