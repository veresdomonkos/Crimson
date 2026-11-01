#pragma once
#include "frame_context.hpp"
#include "frame_data.hpp"
#include "render_stats.hpp"
#include "resource_manager.hpp"

namespace crimson
{
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual FrameContext BeginFrame(const FrameLightingData& lighting) = 0;
        virtual void EndFrame(const FrameContext& frameContext) = 0;

        virtual void SetShadowMap(TextureHandle shadowMap) = 0;

        // Stats of the last finished frame
        [[nodiscard]] const RenderStats& GetStats() const { return m_lastStats; }
    protected:
        RenderStats m_stats;
        RenderStats m_lastStats;
        FrameGpuData m_gpuData;
    };
}
