#pragma once
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/render_commands.hpp"
#include "crimson/core/command_buffer.hpp"
#include "glm/vec4.hpp"

namespace crimson
{
    class RenderPass
    {
    public:
        explicit RenderPass(const RenderPassInfo& info, RenderCommandBuffer &m_commandBuffer)
            : m_commandBuffer(m_commandBuffer)
        {
            m_commandBuffer.Submit<BeginRenderPassCommand>(info);
        }

        ~RenderPass()
        {
            m_commandBuffer.Submit<EndRenderPassCommand>();
        }

        RenderPass(RenderPass&&) noexcept = delete;
        RenderPass& operator=(RenderPass&&) noexcept = delete;

        void Draw() {}
    private:
        RenderCommandBuffer& m_commandBuffer;
    };

    class FrameContext
    {
    public:
        FrameContext(RenderSurfaceHandle surfaceHandle, RenderTargetHandle defaultTarget, RenderCommandBuffer& commandBuffer)
            : m_surfaceHandle(surfaceHandle), m_commandBuffer(commandBuffer), m_defaultTarget(defaultTarget)
        {}

        RenderPass BeginRenderPass(RenderPassInfo info)
        {
            if (!info.Target)
                info.Target = m_defaultTarget;

            return RenderPass(info, m_commandBuffer);
        }

        RenderCommandBuffer& GetCommandBuffer() { return m_commandBuffer; }
        RenderSurfaceHandle GetSurfaceHandle() const { return m_surfaceHandle; }
    protected:
        RenderSurfaceHandle m_surfaceHandle;
        RenderCommandBuffer& m_commandBuffer;
        RenderTargetHandle m_defaultTarget;
    };
}
