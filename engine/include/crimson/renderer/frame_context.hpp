#pragma once

#include "crimson/renderer/render_commands.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <variant>
#include <vector>

#include "frame_data.hpp"
#include "mesh.hpp"
#include "native_handles.hpp"

namespace crimson
{
    constexpr uint32_t MaxRenderPasses = 64;

    // An index into the frame's object transforms. 0 is always the identity transform.
    using ObjectId = uint32_t;

    struct DrawInfo
    {
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle IndexBuffer;
        MaterialHandle Material;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        ObjectId Object = 0;
    };

    class RenderPass
    {
    public:
        RenderPass() = default;
        explicit RenderPass(const RenderPassInfo& info) : m_info(info) {}

        [[nodiscard]] const RenderPassInfo& Info() const { return m_info; }
        [[nodiscard]] std::span<const DrawInfo> GetDraws() const { return m_drawInfos;}

        // A draw without a material is skipped, so a shader that failed to compile leaves the object out of the frame
        void Draw(const DrawInfo& drawInfo)
        {
            if (drawInfo.Material)
                m_drawInfos.emplace_back(drawInfo);
        }

        // A valid materialOverride replaces the materials of all submeshes, as a shadow pass needs
        void Draw(const Mesh& mesh, ObjectId object = 0, MaterialHandle materialOverride = {})
        {
            for (auto subMesh : mesh.SubMeshes)
            {
                const MaterialHandle material = materialOverride ? materialOverride : mesh.Materials[subMesh.MaterialSlot];
                Draw(DrawInfo{ mesh.VB, mesh.IB, material, subMesh.FirstIndex, subMesh.IndexCount, object });
            }
        }

        // Keeps the draw list's capacity so a pass doesn't reallocate it every frame
        void Reset(const RenderPassInfo& info)
        {
            m_info = info;
            m_drawInfos.clear();
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
        crimson::ClearFlags ClearFlags = crimson::ClearFlags::None;
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
        std::vector<ObjectData> Objects;
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

            PassEntry& entry = m_data->Passes[m_data->PassCount++];

            if (auto* pass = std::get_if<RenderPass>(&entry))
            {
                pass->Reset(info);
                return *pass;
            }

            return entry.emplace<RenderPass>(info);
        }

        void AddRawPass(RawPassInfo info)
        {
            assert(m_data->PassCount < MaxRenderPasses);

            const uint32_t index = m_data->PassCount++;

            if (!info.Target)
                info.Target = m_data->DefaultTarget;

            m_data->Passes[index] = RawPass{info};
        }

        // The id stays valid for this frame and can be drawn in any of its passes
        ObjectId AddObject(const glm::mat4& transform)
        {
            m_data->Objects.push_back({ transform, glm::transpose(glm::inverse(transform)) });
            return static_cast<ObjectId>(m_data->Objects.size() - 1);
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

        [[nodiscard]] std::span<const ObjectData> GetObjects() const { return m_data.Objects; }

        void Init(RenderTargetHandle defaultTarget, bool shouldRender)
        {
            m_data.DefaultTarget = defaultTarget;
            m_data.ShouldRender = shouldRender;
        }

        void Reset()
        {
            for (uint32_t i = 0; i < m_data.PassCount; ++i)
            {
                // Render passes are cleared when they are reused, raw passes release their callback here
                if (std::holds_alternative<RawPass>(m_data.Passes[i]))
                    m_data.Passes[i] = RenderPass{};
            }

            m_data.PassCount = 0;
            m_data.Objects.clear();
            m_data.Objects.push_back({ glm::mat4(1.0f), glm::mat4(1.0f) });
            m_data.DefaultTarget = RenderTargetHandle::Invalid();
            m_data.ShouldRender = false;
        }

        void SetIndex(uint32_t index) { m_data.FrameIndex = index; }

        FrameContext CreateContext() { return FrameContext(m_data); }
    private:
        FrameData m_data;
    };
}
