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
        Frame(RenderSurfaceHandle surfaceHandle, RenderTargetHandle defaultTarget, RenderCommandBuffer& commandBuffer)
            : m_surfaceHandle(surfaceHandle), m_commandBuffer(commandBuffer), m_defaultTarget(defaultTarget)
        {}

        void BeginRenderPass(RenderPassInfo info)
        {
            if (!info.Target)
                info.Target = m_defaultTarget;

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
        RenderTargetHandle m_defaultTarget;
    };
}
