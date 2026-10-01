#pragma once

#include "crimson/renderer/render_commands.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <variant>

#include "native_handles.hpp"

namespace crimson
{
    constexpr uint32_t MaxRenderPasses = 16;

    struct DrawInfo
    {
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle IndexBuffer;
        MaterialHandle Material;
    };

    class RenderPass
    {
    public:
        RenderPass() = default;
        explicit RenderPass(const RenderPassInfo& info) : m_info(info) {}

        [[nodiscard]] const RenderPassInfo& Info() const { return m_info; }
        [[nodiscard]] std::span<const DrawInfo> GetDraws() const { return m_drawInfos;}

        void Draw(const DrawInfo& drawInfo)
        {
            m_drawInfos.emplace_back(drawInfo);
        }

        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        RenderPass(RenderPass&&) = default;
        RenderPass& operator=(RenderPass&&) = default;

    private:
        RenderPassInfo m_info;
        std::vector<DrawInfo> m_drawInfos;
    };

    class RawPass
    {
    public:
        RawPass() = default;
        RawPass(RenderTargetHandle target, RawPassCallback callback)
            : m_target(target), m_callback(std::move(callback)) {}

        [[nodiscard]] RenderTargetHandle Target() const { return m_target; }
        [[nodiscard]] const RawPassCallback& Callback() const { return m_callback; }

        RawPass(const RawPass&) = delete;
        RawPass& operator=(const RawPass&) = delete;
        RawPass(RawPass&&) = default;
        RawPass& operator=(RawPass&&) = default;

    private:
        RenderTargetHandle m_target;
        RawPassCallback m_callback;
    };

    using PassEntry = std::variant<RenderPass, RawPass>;

    struct FrameData
    {
        uint32_t FrameIndex = 0;
        std::array<PassEntry, MaxRenderPasses> Passes;
        uint32_t PassCount = 0;
        RenderTargetHandle DefaultTarget = RenderTargetHandle::Invalid();
        bool ShouldRender = false;
    };

    class FrameContext
    {
    public:
        explicit FrameContext(FrameData& data) : m_data(&data) {}

        RenderPass& BeginRenderPass(RenderPassInfo info)
        {
            assert(m_data->PassCount < MaxRenderPasses);

            if (!info.Target)
                info.Target = m_data->DefaultTarget;

            const uint32_t index = m_data->PassCount++;
            m_data->Passes[index] = RenderPass{info};
            return std::get<RenderPass>(m_data->Passes[index]);
        }

        void AddRawPass(RenderTargetHandle target, RawPassCallback callback)
        {
            assert(m_data->PassCount < MaxRenderPasses);

            const uint32_t index = m_data->PassCount++;

            if (!target)
                target = m_data->DefaultTarget;

            m_data->Passes[index] = RawPass{target, std::move(callback)};
        }

        [[nodiscard]] uint32_t GetIndex() const { return m_data->FrameIndex; }
        [[nodiscard]] bool ShouldRender() const { return m_data->ShouldRender; }
    private:
        FrameData* m_data;
    };

    class Frame
    {
    public:
        [[nodiscard]] std::span<const PassEntry> GetPasses() const
        {
            return std::span(m_data.Passes.data(), m_data.PassCount);
        }

        void Init(RenderTargetHandle defaultTarget, bool shouldRender)
        {
            m_data.DefaultTarget = defaultTarget;
            m_data.ShouldRender = shouldRender;
        }

        void Reset()
        {
            for (uint32_t i = 0; i < m_data.PassCount; ++i)
            {
                m_data.Passes[i] = RenderPass{};
            }

            m_data.PassCount = 0;
            m_data.DefaultTarget = RenderTargetHandle::Invalid();
            m_data.ShouldRender = false;
        }

        void SetIndex(uint32_t index) { m_data.FrameIndex = index; }

        FrameContext CreateContext() { return FrameContext(m_data); }
    private:
        FrameData m_data;
    };
}