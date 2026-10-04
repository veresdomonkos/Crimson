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

    struct RawPassInfo
    {
        RenderTargetHandle Target;
        ClearFlags ClearFlags = ClearFlags::None;
        glm::vec4 ClearColor{0, 0, 0, 1};
        float ClearDepth = 1.0f;
        uint32_t ClearStencil = 0;
        RawPassCallback Callback;
    };

    class RawPass
    {
    public:
        RawPass(RawPassInfo info)
            : m_info(std::move(info))
        {

        }

        [[nodiscard]] const RawPassInfo& Info() const { return m_info; }

        RawPass(const RawPass&) = delete;
        RawPass& operator=(const RawPass&) = delete;
        RawPass(RawPass&&) = default;
        RawPass& operator=(RawPass&&) = default;

    private:
        RawPassInfo m_info;
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

        void AddRawPass(RawPassInfo info)
        {
            assert(m_data->PassCount < MaxRenderPasses);

            const uint32_t index = m_data->PassCount++;

            if (!info.Target)
                info.Target = m_data->DefaultTarget;

            m_data->Passes[index] = RawPass{info};
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