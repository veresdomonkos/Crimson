#pragma once

#include "crimson/renderer/render_commands.hpp"

#include <vector>
#include <cassert>
#include <span>

namespace crimson
{
    constexpr uint32_t MaxRenderPasses = 16;

    struct DrawInfo
    {
    };

    class RenderPass
    {
    public:
        const RenderPassInfo& Info() const
        {
            return m_info;
        }

        [[nodiscard]] std::span<const DrawInfo> GetDraws() const
        {
            return m_drawInfos;
        }

        void Draw(const DrawInfo& drawInfo)
        {
            m_drawInfos.emplace_back(drawInfo);
        }
    private:
        void Reset()
        {
            m_drawInfos.clear();
            m_info = {};
        }
    private:
        friend class Frame;
        friend class FrameContext;

        RenderPassInfo m_info;
        std::vector<DrawInfo> m_drawInfos;
    };

    struct FrameData
    {
        uint32_t FrameIndex = 0;
        RenderPass RenderPasses[MaxRenderPasses];
        uint32_t RenderPassCount = 0;
        RenderTargetHandle DefaultTarget = RenderTargetHandle::Invalid();
        RenderSurfaceHandle Surface = RenderSurfaceHandle::Invalid();
        bool ShouldRender = false;
    };

    class FrameContext
    {
    public:
        FrameContext(FrameData& data) : m_data(&data) {}

        RenderPass& BeginRenderPass(const RenderPassInfo& info)
        {
            assert(m_data->RenderPassCount < MaxRenderPasses);

            auto& pass = m_data->RenderPasses[m_data->RenderPassCount++];
            pass.m_info = info;

            if (!pass.m_info.Target)
                pass.m_info.Target = m_data->DefaultTarget;

            return pass;
        }

        uint32_t GetIndex() const { return m_data->FrameIndex; }
        bool ShouldRender() const { return m_data->ShouldRender; }
    private:
        FrameData* m_data;
    };

    class Frame
    {
    public:
        [[nodiscard]] std::span<const RenderPass> GetRenderPasses() const
        {
            return std::span(m_data.RenderPasses, m_data.RenderPasses + m_data.RenderPassCount);
        }

        void Init(RenderSurfaceHandle surface, RenderTargetHandle defaultTarget, bool shouldRender)
        {
            m_data.DefaultTarget = defaultTarget;
            m_data.Surface = surface;
            m_data.ShouldRender = shouldRender;
        }

        void Reset()
        {
            for (auto it = m_data.RenderPasses; it != m_data.RenderPasses + m_data.RenderPassCount; ++it)
            {
                it->Reset();
            }

            m_data.RenderPassCount = 0;
            m_data.DefaultTarget = RenderTargetHandle::Invalid();
            m_data.Surface = RenderSurfaceHandle::Invalid();
            m_data.ShouldRender = false;
        }

        void SetIndex(uint32_t index) { m_data.FrameIndex = index; }
        [[nodiscard]] RenderSurfaceHandle GetSurface() const { return m_data.Surface; }

        FrameContext CreateContext() { return FrameContext(m_data); }
    private:
       FrameData m_data;
    };
}
