#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core.hpp"
#include "frame_gpu_data.hpp"
#include "frame_table.hpp"
#include "material_properties.hpp"
#include "mesh.hpp"
#include "shader_variant.hpp"

namespace crimson
{
    constexpr uint32_t kMaxFramesInFlight = 2;
    constexpr uint32_t kMaxPassReads = 4;

    // An object transform of the frame, from FrameRecorder::AddObject. None is the identity transform.
    struct ObjectTag;
    using ObjectId = FrameId<ObjectTag>;

    // A view of the frame, from FrameRecorder::AddView. None is the view of the pass a draw is in.
    struct ViewTag;
    using ViewId = FrameId<ViewTag>;

    // The material properties of the frame, from FrameRecorder::AddProperties. None is the material as it is.
    struct MaterialPropertiesTag;
    using MaterialPropertiesId = FrameId<MaterialPropertiesTag>;

    struct DrawItem
    {
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle IndexBuffer;
        MaterialHandle Material;

        // What is set on top of the material for this draw: it is this material, with these properties
        MaterialPropertiesId Properties;

        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        // None is drawn with the identity transform
        ObjectId Object;
        ShaderVariant Variant = ShaderVariant::Main;

        // Squared distance from the view's camera, transparent draws go back to front by it
        float SortDepth = 0.0f;

        // The camera it is drawn with: the pass's own when none, or another of the frame's views
        ViewId View;

        // Limits the draw to a rect of the target, in the same space as PassInfo::Viewport. It is cut to the view's area,
        // and a draw that ends up with nothing left is skipped.
        std::optional<RenderRect> Scissor;

        // Added to each index to find its vertex, so many meshes can share one vertex buffer
        int32_t BaseVertex = 0;
    };

    struct DrawOptions
    {
        // Replaces the materials of all submeshes when set
        MaterialHandle MaterialOverride;

        // Set on top of the material of every submesh, like { .MaterialOverride = red, .Properties = tint }, with the id from
        // FrameRecorder::AddProperties
        MaterialPropertiesId Properties;

        // A depth pass, like the shadows, draws each submesh with its own material's depth version, so what its
        // shader does to the shape shows there too
        ShaderVariant Variant = ShaderVariant::Main;

        // The pass's view unless set. A pass can draw with several, like the shadows with one per atlas tile.
        ViewId View;
    };

    // How a pass's draws are ordered and blended
    enum class PassKind
    {
        // Draws are grouped by state to rebind as little as possible, transparent ones come last and farthest first, and
        // depth is tested and written
        Scene,

        // Draws keep the order they were recorded in, every one is alpha blended, and there is no depth test or depth
        // write. The material decides culling. For what is drawn on top of a scene, such as a UI or gizmos.
        Overlay
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

        // The camera of the pass's draws, unless they name a view of their own
        ViewData View;

        // The textures the pass samples, written by an earlier pass or not at all this frame. The empty ones are ignored.
        std::array<TextureHandle, kMaxPassReads> Reads{};

        PassKind Kind = PassKind::Scene;
    };

    // One pass of a recorded frame: its info and its draws
    class PassRecord
    {
    public:
        [[nodiscard]] const PassInfo& GetInfo() const { return m_info; }
        [[nodiscard]] std::span<const DrawItem> GetDraws() const { return m_draws; }

        // Where the pass's own view is among the frame's views
        [[nodiscard]] ViewId GetView() const { return m_view; }
    private:
        friend class FramePacket;
        friend class FrameRecorder;
        friend class PassBuilder;

        PassInfo m_info;
        ViewId m_view;
        std::vector<DrawItem> m_draws;
    };

    // Everything recorded for one frame. The backends keep one per frame in flight and reuse their memory.
    class FramePacket
    {
    public:
        FramePacket() { Reset(); }

        [[nodiscard]] std::span<const PassRecord> GetPasses() const { return std::span(m_passes.data(), m_passCount); }
        [[nodiscard]] std::span<const GpuObject> GetObjects() const { return m_objects.All(); }
        [[nodiscard]] const GpuObject& GetObject(ObjectId object) const { return m_objects.Get(object); }
        [[nodiscard]] std::span<const FrameView> GetViews() const { return m_views.All(); }
        [[nodiscard]] const FrameView& GetView(ViewId view) const { return m_views.Get(view); }

        // What an object of none stands for. It is the first object, so it is index 0 of the uploaded transforms.
        [[nodiscard]] ObjectId IdentityObject() const { return m_identityObject; }
        [[nodiscard]] const FrameLighting& GetLighting() const { return m_lighting; }

        // What AddProperties was given: the entries, and the bytes their values are at, which the offsets of the entries are
        // relative to
        [[nodiscard]] std::span<const MaterialPropertyEntry> GetPropertyEntries(MaterialPropertiesId properties) const
        {
            const PropertyRange& range = m_properties.Get(properties);
            return std::span(m_propertyEntries).subspan(range.FirstEntry, range.EntryCount);
        }

        [[nodiscard]] std::span<const std::byte> GetPropertyBytes(MaterialPropertiesId properties) const
        {
            const PropertyRange& range = m_properties.Get(properties);
            return std::span(m_propertyBytes).subspan(range.FirstByte, range.ByteCount);
        }

        // Keeps the capacity of the lists, so a frame like the last one doesn't allocate
        void Reset()
        {
            // The draws are cleared when a record is reused
            m_passCount = 0;

            m_objects.Clear();
            m_identityObject = m_objects.Add({ glm::mat4(1.0f), glm::mat4(1.0f) });

            m_views.Clear();

            m_properties.Clear();
            m_propertyEntries.clear();
            m_propertyBytes.clear();

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
        FrameTable<ObjectTag, GpuObject> m_objects;
        ObjectId m_identityObject;
        FrameTable<ViewTag, FrameView> m_views;

        // The entries and bytes of all the properties are in one list each, so a frame like the last one doesn't allocate
        struct PropertyRange
        {
            uint32_t FirstEntry = 0;
            uint32_t EntryCount = 0;
            uint32_t FirstByte = 0;
            uint32_t ByteCount = 0;
        };

        FrameTable<MaterialPropertiesTag, PropertyRange> m_properties;
        std::vector<MaterialPropertyEntry> m_propertyEntries;
        std::vector<std::byte> m_propertyBytes;
        FrameLighting m_lighting;
    };

    // Adds draws to a pass. It refers to the pass by index, so it stays valid while more passes are added.
    class PassBuilder
    {
    public:
        PassBuilder(FramePacket& packet, uint32_t index) : m_packet(&packet), m_index(index) {}

        // A draw without a material is skipped, so a shader that failed to compile leaves the object out of the frame.
        // The recorded draw has the object and the view it ends up with, the identity and the pass's in place of none.
        void Draw(const DrawItem& item)
        {
            if (!item.Material)
                return;

            DrawItem& draw = Record().m_draws.emplace_back(item);
            assert((!item.Properties || m_packet->m_properties.Contains(item.Properties)) && "The properties have to come from this frame's AddProperties");

            draw.Object = ResolveObject(item.Object);
            draw.View = ResolveView(item.View);
        }

        // One draw per submesh, with the material at its slot in materials (one per slot of the mesh, by SubMesh::MaterialSlot).
        // A slot past the end has none, and is skipped like one that failed to load. They sort by the distance to the mesh's center.
        void DrawMesh(const Mesh& mesh, std::span<const MaterialHandle> materials, ObjectId object = {}, const DrawOptions& options = {})
        {
            const glm::vec3 localCenter = (mesh.Bounds.Min + mesh.Bounds.Max) * 0.5f;
            const glm::vec3 center = glm::vec3(m_packet->GetObject(ResolveObject(object)).Transform * glm::vec4(localCenter, 1.0f));
            const glm::vec3 fromCamera = center - m_packet->GetView(ResolveView(options.View)).View.CameraPosition;
            const float depth = glm::dot(fromCamera, fromCamera);

            for (const SubMesh& subMesh : mesh.SubMeshes)
            {
                const MaterialHandle material = options.MaterialOverride ? options.MaterialOverride
                    : subMesh.MaterialSlot < materials.size() ? materials[subMesh.MaterialSlot] : MaterialHandle::Invalid();
                Draw(DrawItem{ mesh.VertexBuffer, mesh.IndexBuffer, material, options.Properties, subMesh.FirstIndex, subMesh.IndexCount, object, options.Variant, depth, options.View });
            }
        }

        [[nodiscard]] const PassInfo& GetInfo() const { return Record().m_info; }
        [[nodiscard]] std::span<const DrawItem> GetDraws() const { return Record().m_draws; }
    private:
        [[nodiscard]] PassRecord& Record() const { return m_packet->m_passes[m_index]; }

        [[nodiscard]] ObjectId ResolveObject(ObjectId object) const
        {
            assert((!object || m_packet->m_objects.Contains(object)) && "The object has to come from this frame's AddObject");
            return object ? object : m_packet->IdentityObject();
        }

        [[nodiscard]] ViewId ResolveView(ViewId view) const
        {
            assert((!view || m_packet->m_views.Contains(view)) && "The view has to come from this frame's AddView");
            return view ? view : Record().m_view;
        }

        FramePacket* m_packet;
        uint32_t m_index;
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
            NextRecord(info);
            return PassBuilder(*m_packet, m_packet->m_passCount - 1);
        }

        // The id stays valid for this frame and can be drawn in any of its passes
        ObjectId AddObject(const glm::mat4& transform)
        {
            return m_packet->m_objects.Add({ transform, glm::transpose(glm::inverse(transform)) });
        }

        // Material properties for draws, which name them by the id. It stays valid for this frame and can be drawn with in any of
        // its passes. Without any properties it is none, the material as it is.
        MaterialPropertiesId AddProperties(const MaterialProperties& properties)
        {
            if (properties.Entries().empty())
                return {};

            FramePacket& packet = *m_packet;
            const FramePacket::PropertyRange range{ static_cast<uint32_t>(packet.m_propertyEntries.size()), static_cast<uint32_t>(properties.Entries().size()),
                                                    static_cast<uint32_t>(packet.m_propertyBytes.size()), static_cast<uint32_t>(properties.Bytes().size()) };

            packet.m_propertyEntries.insert(packet.m_propertyEntries.end(), properties.Entries().begin(), properties.Entries().end());
            packet.m_propertyBytes.insert(packet.m_propertyBytes.end(), properties.Bytes().begin(), properties.Bytes().end());
            return packet.m_properties.Add(range);
        }

        // Another camera for the frame's draws, besides the one each pass has. With a viewport, its draws only reach
        // that part of their pass's target. The id stays valid for this frame and can be drawn with in any of its passes.
        ViewId AddView(const ViewData& view, const std::optional<RenderRect>& viewport = std::nullopt)
        {
            return m_packet->m_views.Add({ view, viewport });
        }

        // Starts empty every frame
        [[nodiscard]] FrameLighting& Lighting() { return m_packet->m_lighting; }

        // Empty once it was moved from, like into Renderer::Submit
        [[nodiscard]] bool IsValid() const { return m_packet != nullptr; }
        [[nodiscard]] const FramePacket& GetPacket() const { return *m_packet; }
    private:
        PassRecord& NextRecord(const PassInfo& info)
        {
            if (m_packet->m_passCount == m_packet->m_passes.size())
                m_packet->m_passes.emplace_back();

            PassRecord& record = m_packet->m_passes[m_packet->m_passCount++];
            record.m_info = info;
            record.m_view = AddView(info.View);
            record.m_draws.clear();
            return record;
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
