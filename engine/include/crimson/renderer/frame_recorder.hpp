#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core.hpp"
#include "frame_gpu_data.hpp"
#include "mesh.hpp"
#include "native_context.hpp"
#include "shader_variant.hpp"

namespace crimson
{
    constexpr uint32_t kMaxPassesPerFrame = 64;
    constexpr uint32_t kMaxFramesInFlight = 2;
    constexpr uint32_t kMaxPassReads = 4;

    // An index into the frame's object transforms. 0 is always the identity transform.
    using ObjectId = uint32_t;

    struct DrawItem
    {
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle IndexBuffer;
        MaterialHandle Material;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        ObjectId Object = 0;
        ShaderVariant Variant = ShaderVariant::Main;

        // Squared distance from the pass's camera, transparent draws go back to front by it
        float SortDepth = 0.0f;
    };

    struct DrawOptions
    {
        // Replaces the materials of all submeshes when set
        MaterialHandle MaterialOverride;

        // A depth pass, like the shadows, draws each submesh with its own material's depth version, so what its
        // shader does to the shape shows there too
        ShaderVariant Variant = ShaderVariant::Main;
    };

    struct PassInfo
    {
        // Shown in the profiler. It has to outlive the frame, so it is meant to be a string literal.
        const char* Name = "Pass";

        // The back buffer when empty
        RenderTargetHandle Target;
        ClearInfo Clear;

        // Limits drawing and clearing to part of the target, the whole target when not set
        std::optional<RenderRect> Viewport;

        ViewData View;

        // The textures the pass samples, written by an earlier pass or not at all this frame. The empty ones are ignored.
        std::array<TextureHandle, kMaxPassReads> Reads{};
    };

    // One pass of a recorded frame: draws, or a native callback in their place
    class PassRecord
    {
    public:
        [[nodiscard]] const PassInfo& GetInfo() const { return m_info; }
        [[nodiscard]] std::span<const DrawItem> GetDraws() const { return m_draws; }

        [[nodiscard]] bool IsNative() const { return static_cast<bool>(m_native); }
        [[nodiscard]] const NativePassFn& GetNativeFn() const { return m_native; }
    private:
        friend class FramePacket;
        friend class FrameRecorder;
        friend class PassBuilder;

        PassInfo m_info;
        std::vector<DrawItem> m_draws;
        NativePassFn m_native;
    };

    // Everything recorded for one frame. The backends keep one per frame in flight and reuse their memory.
    class FramePacket
    {
    public:
        FramePacket() { Reset(); }

        [[nodiscard]] std::span<const PassRecord> GetPasses() const { return std::span(m_passes.data(), m_passCount); }
        [[nodiscard]] std::span<const GpuObject> GetObjects() const { return m_objects; }
        [[nodiscard]] const FrameLighting& GetLighting() const { return m_lighting; }

        // Keeps the capacity of the lists, so a frame like the last one doesn't allocate
        void Reset()
        {
            // The draws are cleared when a record is reused, the callbacks are released now
            for (uint32_t i = 0; i < m_passCount; ++i)
                m_passes[i].m_native = nullptr;

            m_passCount = 0;

            m_objects.clear();
            m_objects.push_back({ glm::mat4(1.0f), glm::mat4(1.0f) });

            m_lighting.AmbientColor = FrameLighting{}.AmbientColor;
            m_lighting.Lights.clear();
            m_lighting.Shadows.clear();
        }
    private:
        friend class FrameRecorder;
        friend class PassBuilder;

        // Records past m_passCount are left over from earlier frames, kept for their draw lists' capacity
        std::vector<PassRecord> m_passes;
        uint32_t m_passCount = 0;
        std::vector<GpuObject> m_objects;
        FrameLighting m_lighting;
    };

    // Adds draws to a pass. It refers to the pass by index, so it stays valid while more passes are added.
    class PassBuilder
    {
    public:
        PassBuilder() = default;
        PassBuilder(FramePacket& packet, uint32_t index) : m_packet(&packet), m_index(index) {}

        // A draw without a material is skipped, so a shader that failed to compile leaves the object out of the frame
        void Draw(const DrawItem& item)
        {
            if (m_packet && item.Material)
                Record().m_draws.push_back(item);
        }

        // One draw per submesh, each with its slot's material. They sort by the distance to the mesh's center.
        void DrawMesh(const Mesh& mesh, ObjectId object = 0, const DrawOptions& options = {})
        {
            if (!m_packet)
                return;

            const glm::vec3 localCenter = (mesh.Bounds.Min + mesh.Bounds.Max) * 0.5f;
            const glm::vec3 center = glm::vec3(m_packet->m_objects[object].Transform * glm::vec4(localCenter, 1.0f));
            const glm::vec3 fromCamera = center - Record().m_info.View.CameraPosition;
            const float depth = glm::dot(fromCamera, fromCamera);

            for (const SubMesh& subMesh : mesh.SubMeshes)
            {
                const MaterialHandle material = options.MaterialOverride ? options.MaterialOverride : mesh.Materials[subMesh.MaterialSlot];
                Draw(DrawItem{ mesh.VertexBuffer, mesh.IndexBuffer, material, subMesh.FirstIndex, subMesh.IndexCount, object, options.Variant, depth });
            }
        }

        // False for a pass that was dropped, as the frame already had kMaxPassesPerFrame
        [[nodiscard]] bool IsValid() const { return m_packet != nullptr; }

        [[nodiscard]] const PassInfo& GetInfo() const { return Read().m_info; }
        [[nodiscard]] std::span<const DrawItem> GetDraws() const { return Read().m_draws; }
    private:
        // Only for a valid builder
        [[nodiscard]] PassRecord& Record() const { return m_packet->m_passes[m_index]; }

        // A dropped pass reads as an empty one
        [[nodiscard]] const PassRecord& Read() const
        {
            static const PassRecord dropped;
            return m_packet ? Record() : dropped;
        }

        FramePacket* m_packet = nullptr;
        uint32_t m_index = 0;
    };

    // Records a frame into a packet, from Renderer::BeginRecording to Renderer::Submit. Passes run in the order they are added.
    class FrameRecorder
    {
    public:
        explicit FrameRecorder(FramePacket& packet) : m_packet(&packet) {}

        FrameRecorder(const FrameRecorder&) = delete;
        FrameRecorder& operator=(const FrameRecorder&) = delete;

        FrameRecorder(FrameRecorder&& other) noexcept : m_packet(std::exchange(other.m_packet, nullptr)) {}
        FrameRecorder& operator=(FrameRecorder&& other) noexcept
        {
            m_packet = std::exchange(other.m_packet, nullptr);
            return *this;
        }

        PassBuilder AddPass(const PassInfo& info)
        {
            PassRecord* record = NextRecord(info);
            return record ? PassBuilder(*m_packet, m_packet->m_passCount - 1) : PassBuilder();
        }

        // The callback runs during Renderer::Submit, so it captures by value, or references what lives longer than a frame
        void AddNativePass(const PassInfo& info, NativePassFn fn)
        {
            if (PassRecord* record = NextRecord(info))
                record->m_native = std::move(fn);
        }

        // The id stays valid for this frame and can be drawn in any of its passes
        ObjectId AddObject(const glm::mat4& transform)
        {
            m_packet->m_objects.push_back({ transform, glm::transpose(glm::inverse(transform)) });
            return static_cast<ObjectId>(m_packet->m_objects.size() - 1);
        }

        // Starts empty every frame
        [[nodiscard]] FrameLighting& Lighting() { return m_packet->m_lighting; }

        // Empty once it was moved from, like into Renderer::Submit
        [[nodiscard]] bool IsValid() const { return m_packet != nullptr; }
        [[nodiscard]] const FramePacket& GetPacket() const { return *m_packet; }
    private:
        PassRecord* NextRecord(const PassInfo& info)
        {
            assert(m_packet->m_passCount < kMaxPassesPerFrame && "Too many passes in one frame");
            if (m_packet->m_passCount >= kMaxPassesPerFrame)
                return nullptr;

            if (m_packet->m_passCount == m_packet->m_passes.size())
                m_packet->m_passes.emplace_back();

            PassRecord& record = m_packet->m_passes[m_packet->m_passCount++];
            record.m_info = info;
            record.m_draws.clear();
            record.m_native = nullptr;
            return &record;
        }

        FramePacket* m_packet;
    };

    // The packets of the frames in flight, handed out in turn
    template <uint32_t Count>
    class FramePacketRing
    {
    public:
        FramePacket& Next()
        {
            m_index = (m_index + 1) % Count;
            return m_packets[m_index];
        }
    private:
        std::array<FramePacket, Count> m_packets;
        uint32_t m_index = Count - 1;
    };
}
