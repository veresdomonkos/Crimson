#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <type_traits>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <crimson/renderer/draw_sort.hpp>
#include <crimson/renderer/frame_gpu_data.hpp>
#include <crimson/renderer/frame_recorder.hpp>
#include <crimson/renderer/frame_validation.hpp>

#include "renderer/draw_material.hpp"

using namespace crimson;

namespace
{
    constexpr MaterialHandle kMaterial{ 1, 1 };
    constexpr MaterialHandle kOverride{ 2, 1 };

    // BuildDrawOrder only needs something with GetMaterial, so no real resource manager is involved
    struct FakeMaterial
    {
        ShaderHandle Shader;
        MaterialSettings Settings;
    };

    struct FakeMaterials
    {
        std::vector<FakeMaterial> Materials;

        [[nodiscard]] const FakeMaterial& GetMaterial(MaterialHandle handle) const { return Materials[handle.GetId()]; }
    };

    DrawItem MakeDraw(uint32_t material, uint32_t vertexBuffer = 0)
    {
        return DrawItem{ .VertexBuffer = VertexBufferHandle{ vertexBuffer, 1 }, .Material = MaterialHandle{ material, 1 } };
    }

    std::vector<uint32_t> DrawOrder(const std::vector<DrawItem>& draws, const FakeMaterials& materials, PassKind kind = PassKind::Scene)
    {
        std::vector<DrawSortEntry> order;
        BuildDrawOrder(std::span<const DrawItem>(draws), materials, order, kind);

        std::vector<uint32_t> indices;
        for (const DrawSortEntry& entry : order)
            indices.push_back(entry.Index);
        return indices;
    }

    // Render targets and the textures they write, for the validation
    struct FakeTargets
    {
        std::map<uint64_t, std::vector<TextureHandle>> Attachments;

        [[nodiscard]] std::vector<TextureHandle> operator()(RenderTargetHandle target) const { return Attachments.at(target.GetRaw()); }
    };
}

TEST_CASE("A pass keeps what it was added with", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    constexpr RenderTargetHandle target{ 9, 1 };
    const PassBuilder pass = frame.AddPass({ .Name = "Shadows", .Target = target, .Clear = { .Flags = ClearFlags::Depth } });

    CHECK(pass.GetInfo().Target == target);
    CHECK(pass.GetInfo().Clear.Flags == ClearFlags::Depth);

    SECTION("Without a target it stays empty, for the renderer to draw it to the back buffer")
    {
        CHECK_FALSE(frame.AddPass({}).GetInfo().Target);
    }

    CHECK(packet.GetPasses().size() >= 1);
    CHECK(std::string_view(packet.GetPasses()[0].GetInfo().Name) == "Shadows");
}

TEST_CASE("A pass builder stays valid while more passes are added", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    PassBuilder first = frame.AddPass({});

    // Enough to make the list of passes grow and move
    for (int i = 0; i < 20; ++i)
        frame.AddPass({}).Draw(DrawItem{ .Material = kOverride });

    first.Draw(DrawItem{ .Material = kMaterial });

    REQUIRE(packet.GetPasses()[0].GetDraws().size() == 1);
    CHECK(packet.GetPasses()[0].GetDraws()[0].Material == kMaterial);
}

TEST_CASE("A draw without a material is left out", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);
    PassBuilder pass = frame.AddPass({});

    pass.Draw(DrawItem{ .Material = MaterialHandle::Invalid() });
    CHECK(pass.GetDraws().empty());

    pass.Draw(DrawItem{ .Material = kMaterial });
    CHECK(pass.GetDraws().size() == 1);
}

TEST_CASE("Drawing a mesh makes one draw per submesh", "[frame]")
{
    Mesh mesh;
    const std::array materials = { kMaterial, MaterialHandle::Invalid() };
    mesh.SubMeshes = { { .FirstIndex = 0, .IndexCount = 3, .MaterialSlot = 0 }, { .FirstIndex = 3, .IndexCount = 6, .MaterialSlot = 1 } };

    FramePacket packet;
    FrameRecorder frame(packet);
    PassBuilder pass = frame.AddPass({});

    SECTION("Each with its own material, and the one that failed to load is skipped")
    {
        const ObjectId object = frame.AddObject(glm::mat4(1.0f));
        pass.DrawMesh(mesh, materials, object);

        REQUIRE(pass.GetDraws().size() == 1);
        CHECK(pass.GetDraws()[0].Material == kMaterial);
        CHECK(pass.GetDraws()[0].IndexCount == 3);
        CHECK(pass.GetDraws()[0].Object == object);
        CHECK(pass.GetDraws()[0].Variant == ShaderVariant::Main);
    }

    SECTION("A slot past the materials is skipped")
    {
        pass.DrawMesh(mesh, std::span(materials).first(1));

        REQUIRE(pass.GetDraws().size() == 1);
        CHECK(pass.GetDraws()[0].IndexCount == 3);

        pass.DrawMesh(mesh, {});
        CHECK(pass.GetDraws().size() == 1);
    }

    SECTION("An override replaces all of them")
    {
        pass.DrawMesh(mesh, materials, {}, { .MaterialOverride = kOverride });

        REQUIRE(pass.GetDraws().size() == 2);
        CHECK(pass.GetDraws()[0].Material == kOverride);
        CHECK(pass.GetDraws()[1].Material == kOverride);
    }

    SECTION("A depth pass keeps each one's own material, in its depth version")
    {
        pass.DrawMesh(mesh, materials, {}, { .Variant = ShaderVariant::Depth });

        REQUIRE(pass.GetDraws().size() == 1);
        CHECK(pass.GetDraws()[0].Material == kMaterial);
        CHECK(pass.GetDraws()[0].Variant == ShaderVariant::Depth);
    }
}

TEST_CASE("Frame ids are none by default, and each kind is its own type", "[frame]")
{
    static_assert(!std::is_convertible_v<ObjectId, ViewId>);
    static_assert(!std::is_convertible_v<ViewId, ObjectId>);
    static_assert(!std::is_convertible_v<uint32_t, ObjectId>);

    CHECK_FALSE(ObjectId{}.IsValid());
    CHECK_FALSE(ViewId{}.IsValid());
    CHECK(ObjectId{ 3 }.Index() == 3);

    FrameTable<ObjectTag, int> table;
    CHECK_FALSE(table.Contains(ObjectId{}));

    const ObjectId first = table.Add(10);
    const ObjectId second = table.Add(20);
    const ObjectId third = table.Add(30);

    CHECK(first.Index() == 0);
    CHECK(second.Index() == 1);
    CHECK(third.Index() == 2);
    CHECK(table.Get(second) == 20);
    CHECK(table.Size() == 3);

    const std::span<const int> all = table.All();
    REQUIRE(all.size() == 3);
    CHECK(all[0] == 10);
    CHECK(all[2] == 30);

    CHECK(table.Contains(third));
    CHECK_FALSE(table.Contains(ObjectId{ 3 }));

    table.Clear();
    CHECK(table.Size() == 0);
    CHECK_FALSE(table.Contains(first));
    CHECK(table.Add(40).Index() == 0);
}

TEST_CASE("A draw without an object uses the identity", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

    // The identity is the first object, so it is index 0 of what is uploaded
    CHECK(packet.IdentityObject().Index() == 0);
    CHECK(frame.AddObject(moved).Index() == 1);
    CHECK(frame.AddObject(moved).Index() == 2);

    REQUIRE(packet.GetObjects().size() == 3);
    CHECK(packet.GetObjects()[0].Transform == glm::mat4(1.0f));
    CHECK(packet.GetObjects()[1].Transform == moved);

    PassBuilder pass = frame.AddPass({});
    pass.Draw(DrawItem{ .Material = kMaterial });
    pass.Draw(DrawItem{ .Material = kMaterial, .Object = ObjectId{ 1 } });

    REQUIRE(pass.GetDraws().size() == 2);
    CHECK(pass.GetDraws()[0].Object == packet.IdentityObject());
    CHECK(pass.GetDraws()[1].Object == ObjectId{ 1 });

    // The identity is the first again after a reset
    packet.Reset();
    CHECK(packet.IdentityObject().Index() == 0);
    CHECK(frame.AddObject(moved).Index() == 1);
}

TEST_CASE("Resetting a packet leaves it empty for the next frame", "[frame]")
{
    FramePacket packet;

    {
        FrameRecorder frame(packet);
        frame.AddPass({}).Draw(DrawItem{ .Material = kMaterial });
        frame.AddObject(glm::mat4(1.0f));
        frame.Lighting().Lights.push_back({});
    }

    packet.Reset();

    CHECK(packet.GetPasses().empty());
    CHECK(packet.GetObjects().size() == 1);
    CHECK(packet.GetLighting().Lights.empty());

    SECTION("A reused pass doesn't keep the last frame's draws")
    {
        FrameRecorder frame(packet);
        CHECK(frame.AddPass({}).GetDraws().empty());
    }
}

TEST_CASE("The renderer hands out its packets in turn, emptied", "[frame]")
{
    FramePacketRing<2> ring;

    FramePacket& first = ring.Next();
    FramePacket& second = ring.Next();

    CHECK(&first != &second);
    CHECK(&ring.Next() == &first);
}

TEST_CASE("A recorder can't be used once it was moved from", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);
    CHECK(frame.IsValid());

    const FrameRecorder submitted = std::move(frame);
    CHECK(submitted.IsValid());
    CHECK_FALSE(frame.IsValid()); // NOLINT(bugprone-use-after-move): that's what is being checked
}

TEST_CASE("Validation finds passes that sample what they shouldn't", "[frame]")
{
    constexpr RenderTargetHandle kShadowTarget{ 1, 1 };
    constexpr RenderTargetHandle kSceneTarget{ 2, 1 };
    constexpr TextureHandle kShadowAtlas{ 10, 1 };
    constexpr TextureHandle kSceneColor{ 11, 1 };
    constexpr TextureHandle kSceneDepth{ 12, 1 };

    FakeTargets targets;
    targets.Attachments[kShadowTarget.GetRaw()] = { kShadowAtlas };
    targets.Attachments[kSceneTarget.GetRaw()] = { kSceneColor, kSceneDepth };

    FramePacket packet;
    FrameRecorder frame(packet);
    std::vector<PassProblem> problems;

    SECTION("Nothing wrong with sampling what an earlier pass wrote, or something no pass writes")
    {
        frame.AddPass({ .Name = "Shadows", .Target = kShadowTarget });
        frame.AddPass({ .Name = "Scene", .Target = kSceneTarget, .Reads = { kShadowAtlas, TextureHandle{ 99, 1 } } });
        frame.AddPass({ .Name = "Tone Mapping", .Reads = { kSceneColor } });

        FindPassProblems(packet.GetPasses(), targets, problems);
        CHECK(problems.empty());
    }

    SECTION("Sampling the target the pass draws to")
    {
        frame.AddPass({ .Name = "Scene", .Target = kSceneTarget, .Reads = { kSceneDepth } });

        FindPassProblems(packet.GetPasses(), targets, problems);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0] == PassProblem{ 0, kSceneDepth, PassProblemKind::ReadsOwnTarget, 0 });
    }

    SECTION("Sampling something before the pass that writes it")
    {
        frame.AddPass({ .Name = "Scene", .Target = kSceneTarget, .Reads = { kShadowAtlas } });
        frame.AddPass({ .Name = "Shadows", .Target = kShadowTarget });

        FindPassProblems(packet.GetPasses(), targets, problems);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0] == PassProblem{ 0, kShadowAtlas, PassProblemKind::ReadBeforeWrite, 1 });
    }

    SECTION("Nothing wrong with sampling what an earlier pass wrote, when a later one writes it again")
    {
        frame.AddPass({ .Name = "Shadows", .Target = kShadowTarget });
        frame.AddPass({ .Name = "Scene", .Target = kSceneTarget, .Reads = { kShadowAtlas } });
        frame.AddPass({ .Name = "More shadows", .Target = kShadowTarget });

        FindPassProblems(packet.GetPasses(), targets, problems);
        CHECK(problems.empty());
    }
}

TEST_CASE("Opaque draws are grouped to rebind as little as possible", "[draw sort]")
{
    FakeMaterials materials;
    materials.Materials = {
        { ShaderHandle{ 2, 1 }, {} },
        { ShaderHandle{ 1, 1 }, {} },
        { ShaderHandle{ 2, 1 }, {} }
    };

    SECTION("By shader first")
    {
        const std::vector<DrawItem> draws = { MakeDraw(0), MakeDraw(1), MakeDraw(2) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 1, 0, 2 });
    }

    SECTION("Then by material, then by vertex buffer")
    {
        const std::vector<DrawItem> draws = { MakeDraw(2, 1), MakeDraw(0, 5), MakeDraw(2, 0), MakeDraw(0, 1) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 3, 1, 2, 0 });
    }

    SECTION("Identical draws keep their order")
    {
        const std::vector<DrawItem> draws = { MakeDraw(0), MakeDraw(0), MakeDraw(0) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 0, 1, 2 });
    }
}

TEST_CASE("Transparent draws come after opaque ones, farthest first", "[draw sort]")
{
    FakeMaterials materials;
    materials.Materials = {
        { ShaderHandle{ 3, 1 }, {} },
        { ShaderHandle{ 1, 1 }, { .RenderMode = MaterialRenderMode::Transparent } },
        { ShaderHandle{ 2, 1 }, { .RenderMode = MaterialRenderMode::Transparent } }
    };

    const auto at = [](uint32_t material, float depth)
    {
        DrawItem draw = MakeDraw(material);
        draw.SortDepth = depth;
        return draw;
    };

    SECTION("Whatever their material")
    {
        const std::vector<DrawItem> draws = { at(2, 1.0f), at(1, 4.0f), at(0, 9.0f), at(2, 16.0f) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 2, 3, 1, 0 });
    }

    SECTION("At the same depth, in the order they were submitted")
    {
        const std::vector<DrawItem> draws = { MakeDraw(2), MakeDraw(1), MakeDraw(0), MakeDraw(2) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 2, 0, 1, 3 });
    }

    SECTION("A depth that isn't a distance counts as 0")
    {
        const std::vector<DrawItem> draws = { at(1, -5.0f), at(1, 2.0f), at(1, std::numeric_limits<float>::quiet_NaN()) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 1, 0, 2 });
    }
}

TEST_CASE("A transparent material's depth version sorts like an opaque one", "[draw sort]")
{
    FakeMaterials materials;
    materials.Materials = {
        { ShaderHandle{ 2, 1 }, {} },
        { ShaderHandle{ 1, 1 }, { .RenderMode = MaterialRenderMode::Transparent } }
    };

    DrawItem transparent = MakeDraw(1);
    transparent.Variant = ShaderVariant::Depth;
    DrawItem opaque = MakeDraw(0);
    opaque.Variant = ShaderVariant::Depth;

    // Grouped by shader, so the transparent material's lower shader id goes first
    const std::vector<DrawItem> draws = { opaque, transparent };
    CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 1, 0 });
}

TEST_CASE("A mesh's draws sort by the distance from the camera to its center", "[frame]")
{
    Mesh mesh;
    const std::array materials = { kMaterial, kOverride };
    mesh.SubMeshes = { { .MaterialSlot = 0 }, { .MaterialSlot = 1 } };
    mesh.Bounds = { glm::vec3(-1.0f), glm::vec3(3.0f) }; // centered on (1, 1, 1)

    FramePacket packet;
    FrameRecorder frame(packet);

    const ObjectId object = frame.AddObject(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f)));
    PassBuilder pass = frame.AddPass({ .View = { .CameraPosition = glm::vec3(1.0f, 1.0f, 2.0f) } });
    pass.DrawMesh(mesh, materials, object);

    // The center ends up at (1, 1, -1), 3 away from the camera
    REQUIRE(pass.GetDraws().size() == 2);
    CHECK(pass.GetDraws()[0].SortDepth == 9.0f);
    CHECK(pass.GetDraws()[1].SortDepth == 9.0f);
}

TEST_CASE("A draw uses its pass's view unless it names one", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    const ViewData camera{ .ViewProj = glm::mat4(2.0f), .CameraPosition = glm::vec3(1.0f, 2.0f, 3.0f) };
    const RenderRect tile{ 64, 128, 32, 32 };

    PassBuilder first = frame.AddPass({});
    PassBuilder second = frame.AddPass({ .View = camera });
    const ViewId extra = frame.AddView({ .ViewProj = glm::mat4(3.0f) }, tile);

    second.Draw(DrawItem{ .Material = kMaterial });
    second.Draw(DrawItem{ .Material = kMaterial, .View = extra });
    second.Draw(DrawItem{ .Material = kMaterial });
    first.Draw(DrawItem{ .Material = kMaterial });

    const std::span<const PassRecord> passes = packet.GetPasses();
    const std::span<const FrameView> views = packet.GetViews();

    // One for each pass and the one that was added
    REQUIRE(views.size() == 3);
    CHECK(passes[0].GetView() != passes[1].GetView());

    // The pass's view is the camera it was added with, and covers the pass's area
    CHECK(views[passes[1].GetView().Index()].View.ViewProj == camera.ViewProj);
    CHECK(views[passes[1].GetView().Index()].View.CameraPosition == camera.CameraPosition);
    CHECK_FALSE(views[passes[1].GetView().Index()].Viewport);

    REQUIRE(views[extra.Index()].Viewport);
    CHECK(views[extra.Index()].Viewport->X == tile.X);
    CHECK(views[extra.Index()].Viewport->Y == tile.Y);
    CHECK(views[extra.Index()].Viewport->Width == tile.Width);
    CHECK(views[extra.Index()].Viewport->Height == tile.Height);

    // Naming a view is for that draw only, the next one is back to the pass's
    REQUIRE(second.GetDraws().size() == 3);
    CHECK(second.GetDraws()[0].View == passes[1].GetView());
    CHECK(second.GetDraws()[1].View == extra);
    CHECK(second.GetDraws()[2].View == passes[1].GetView());

    REQUIRE(first.GetDraws().size() == 1);
    CHECK(first.GetDraws()[0].View == passes[0].GetView());
}

TEST_CASE("A mesh drawn with a view of its own sorts by the distance from that view", "[frame]")
{
    Mesh mesh;
    const std::array materials = { kMaterial };
    mesh.SubMeshes = { { .MaterialSlot = 0 } };
    mesh.Bounds = { glm::vec3(-1.0f), glm::vec3(1.0f) };

    FramePacket packet;
    FrameRecorder frame(packet);

    PassBuilder pass = frame.AddPass({ .View = { .CameraPosition = glm::vec3(0.0f, 0.0f, 2.0f) } });
    const ViewId far = frame.AddView({ .CameraPosition = glm::vec3(0.0f, 0.0f, 5.0f) });

    pass.DrawMesh(mesh, materials);
    pass.DrawMesh(mesh, materials, {}, { .View = far });

    REQUIRE(pass.GetDraws().size() == 2);
    CHECK(pass.GetDraws()[0].SortDepth == 4.0f);
    CHECK(pass.GetDraws()[1].SortDepth == 25.0f);
    CHECK(pass.GetDraws()[1].View == far);
}

TEST_CASE("The frame's views are uploaded in the order they were added", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    frame.AddPass({ .View = { .ViewProj = glm::mat4(2.0f), .CameraPosition = glm::vec3(1.0f, 2.0f, 3.0f) } });
    frame.AddView({ .ViewProj = glm::mat4(3.0f) });

    FrameGpuData data;
    data.Build(FrameLighting{}, packet.GetViews(), FrameTime{});

    REQUIRE(data.Views.size() == 2);
    CHECK(data.Views[0].ViewProj == glm::mat4(2.0f));
    CHECK(data.Views[0].CameraPosition == glm::vec4(1.0f, 2.0f, 3.0f, 0.0f));
    CHECK(data.Views[1].ViewProj == glm::mat4(3.0f));

    // Nothing is left over for the next frame
    packet.Reset();
    CHECK(packet.GetViews().empty());
}

TEST_CASE("A view's area is clipped to its pass's", "[frame]")
{
    const RenderRect passArea{ 0, 0, 512, 512 };

    const auto equal = [](const RenderRect& a, const RenderRect& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
    };

    SECTION("A view without a viewport has the whole pass")
    {
        CHECK(equal(ViewArea(FrameView{}, passArea), passArea));
    }

    SECTION("A viewport inside the pass stays as it is")
    {
        const RenderRect tile{ 256, 128, 128, 128 };
        CHECK(equal(ViewArea(FrameView{ .Viewport = tile }, passArea), tile));
    }

    SECTION("A viewport across the pass's edge keeps only its part inside")
    {
        CHECK(equal(ViewArea(FrameView{ .Viewport = RenderRect{ 448, -64, 128, 128 } }, passArea), RenderRect{ 448, 0, 64, 64 }));
    }

    SECTION("A viewport outside the pass is empty")
    {
        CHECK(IsEmpty(ViewArea(FrameView{ .Viewport = RenderRect{ 1024, 0, 512, 512 } }, passArea)));
        CHECK(IsEmpty(ViewArea(FrameView{ .Viewport = RenderRect{ 512, 0, 64, 64 } }, passArea)));
    }
}

TEST_CASE("The frame clock starts at 0 and counts the time between frames", "[frame]")
{
    using namespace std::chrono_literals;

    FrameClock clock;
    const auto start = std::chrono::steady_clock::now();

    CHECK(clock.Tick(start) == FrameTime{ 0.0f, 0.0f });
    CHECK(clock.Tick(start + 500ms) == FrameTime{ 0.5f, 0.5f });
    CHECK(clock.Tick(start + 750ms) == FrameTime{ 0.75f, 0.25f });
}

TEST_CASE("The frame block carries the frame's time", "[frame]")
{
    FrameGpuData data;
    data.Build(FrameLighting{}, {}, FrameTime{ 2.5f, 0.125f });

    CHECK(data.Block.Time == 2.5f);
    CHECK(data.Block.DeltaTime == 0.125f);
}

TEST_CASE("An overlay pass keeps its draws in the order they were recorded", "[draw sort]")
{
    FakeMaterials materials;
    materials.Materials = {
        { ShaderHandle{ 3, 1 }, {} },
        { ShaderHandle{ 1, 1 }, { .RenderMode = MaterialRenderMode::Transparent } },
        { ShaderHandle{ 2, 1 }, {} }
    };

    const auto at = [](uint32_t material, float depth)
    {
        DrawItem draw = MakeDraw(material);
        draw.SortDepth = depth;
        return draw;
    };

    // A scene pass would draw the opaque ones first, grouped by shader, and the transparent one last
    const std::vector<DrawItem> draws = { at(1, 4.0f), at(0, 1.0f), at(2, 9.0f), at(1, 16.0f), at(0, 1.0f) };

    CHECK(DrawOrder(draws, materials, PassKind::Scene) != std::vector<uint32_t>{ 0, 1, 2, 3, 4 });
    CHECK(DrawOrder(draws, materials, PassKind::Overlay) == std::vector<uint32_t>{ 0, 1, 2, 3, 4 });
}

namespace
{
    template <typename T>
    T ReadValue(std::span<const std::byte> bytes, uint32_t offset)
    {
        T value;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }
}

TEST_CASE("Material properties are kept for the frame, by id", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    const TextureHandle texture{ 7, 1 };

    SECTION("A property is named by the id of its name")
    {
        CHECK(PropertyId("Color") == PropertyId("Color"));
        CHECK_FALSE(PropertyId("Color") == PropertyId("Colour"));
    }

    SECTION("None is the default, and the ids count up")
    {
        static_assert(!std::is_convertible_v<MaterialPropertiesId, ObjectId>);

        CHECK_FALSE(MaterialPropertiesId{}.IsValid());
        CHECK_FALSE(frame.AddProperties(MaterialProperties()).IsValid());

        const MaterialPropertiesId first = frame.AddProperties(MaterialProperties().Set("Color", glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)).SetTexture("Albedo", texture));
        const MaterialPropertiesId second = frame.AddProperties(MaterialProperties().Set("Tint", 2.0f));

        CHECK(first.Index() == 0);
        CHECK(second.Index() == 1);

        // The entries and bytes read back as given, with the offsets relative to their own properties
        const auto entries = packet.GetPropertyEntries(first);
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].Id == PropertyId("Color"));
        CHECK(entries[0].Size == sizeof(glm::vec4));
        CHECK(ReadValue<glm::vec4>(packet.GetPropertyBytes(first), entries[0].Offset) == glm::vec4(1.0f, 0.5f, 0.25f, 1.0f));
        CHECK(entries[1].Id == PropertyId("Albedo"));
        CHECK(entries[1].Texture == texture);
        CHECK(entries[1].Size == 0);

        const auto others = packet.GetPropertyEntries(second);
        REQUIRE(others.size() == 1);
        CHECK(others[0].Offset == 0);
        CHECK(packet.GetPropertyBytes(second).size() == sizeof(float));
        CHECK(ReadValue<float>(packet.GetPropertyBytes(second), others[0].Offset) == 2.0f);
    }

    SECTION("Setting an id again keeps the last value")
    {
        MaterialProperties properties;
        properties.Set("A", 1.0f).Set("B", 2.0f).Set("A", 3.0f);

        REQUIRE(properties.Entries().size() == 2);
        REQUIRE(properties.Bytes().size() == 2 * sizeof(float));

        for (const MaterialPropertyEntry& entry : properties.Entries())
        {
            const float value = ReadValue<float>(properties.Bytes(), entry.Offset);
            CHECK(value == (entry.Id == PropertyId("A") ? 3.0f : 2.0f));
        }

        // A texture replaces a value of the same id
        properties.SetTexture("A", texture);
        REQUIRE(properties.Entries().size() == 2);
        CHECK(properties.Bytes().size() == sizeof(float));

        // And an invalid one leaves it to the material
        properties.SetTexture("A", TextureHandle::Invalid());
        CHECK(properties.Entries().size() == 1);
    }

    SECTION("Resetting the packet empties them")
    {
        frame.AddProperties(MaterialProperties().Set("Tint", 2.0f));
        packet.Reset();

        const MaterialPropertiesId after = frame.AddProperties(MaterialProperties().Set("Tint", 3.0f));
        CHECK(after.Index() == 0);
        CHECK(ReadValue<float>(packet.GetPropertyBytes(after), 0) == 3.0f);
    }
}

TEST_CASE("A mesh drawn with properties puts them on every submesh draw", "[frame]")
{
    Mesh mesh;
    mesh.SubMeshes = { { .FirstIndex = 0, .IndexCount = 3, .MaterialSlot = 0 }, { .FirstIndex = 3, .IndexCount = 6, .MaterialSlot = 0 } };

    FramePacket packet;
    FrameRecorder frame(packet);
    PassBuilder pass = frame.AddPass({});

    const MaterialPropertiesId tint = frame.AddProperties(MaterialProperties().Set("Tint", 2.0f));
    pass.DrawMesh(mesh, {}, {}, { .MaterialOverride = kOverride, .Properties = tint });
    pass.DrawMesh(mesh, {}, {}, { .MaterialOverride = kOverride });

    const std::span<const DrawItem> draws = pass.GetDraws();
    REQUIRE(draws.size() == 4);
    CHECK(draws[0].Properties == tint);
    CHECK(draws[1].Properties == tint);
    CHECK_FALSE(draws[2].Properties.IsValid());
    CHECK_FALSE(draws[3].Properties.IsValid());
}

namespace
{
    // What the shared property rules read of the resource managers, as a stand-in
    struct FakeShading
    {
        struct Material
        {
            ShaderHandle Shader;
        };

        ShaderInterface Interface;

        [[nodiscard]] Material GetMaterial(MaterialHandle) const { return { ShaderHandle{ 1, 1 } }; }
        [[nodiscard]] const ShaderInterface& GetShaderInterface(ShaderHandle) const { return Interface; }
    };

    ShaderInterface MakeTintedShader()
    {
        ShaderInterface shader;
        shader.MaterialBlockSize = 16;
        shader.Properties.emplace(PropertyId("Color"), ShaderPropertyInfo{ .Offset = 0, .Size = 12, .Name = "Color" });
        shader.Properties.emplace(PropertyId("Amount"), ShaderPropertyInfo{ .Offset = 12, .Size = 4, .Name = "Amount" });
        shader.Textures.emplace(PropertyId("Albedo"), ShaderMaterialTexture{ .Binding = 1, .Name = "Albedo" });
        shader.Textures.emplace(PropertyId("Normal"), ShaderMaterialTexture{ .Binding = 2, .Name = "Normal" });
        return shader;
    }
}

TEST_CASE("A draw's material block and textures follow its properties", "[frame]")
{
    const ShaderInterface shader = MakeTintedShader();

    std::array<float, 4> materialValues = { 1.0f, 2.0f, 3.0f, 4.0f };
    const auto materialBytes = std::as_bytes(std::span(materialValues));

    SECTION("The block is the material's with the values written over it")
    {
        const MaterialProperties properties = MaterialProperties().Set("Amount", 9.0f).Set("Missing", 5.0f).Set("Color", glm::vec4(0.0f));

        std::array<float, 4> block{};
        WriteMaterialBlock(std::as_writable_bytes(std::span(block)), materialBytes, shader, properties.Entries(), properties.Bytes());

        // Amount fits and is written. Color is a vec4 for a property of 12 bytes, so it is too big and the material's stays.
        CHECK(block == std::array<float, 4>{ 1.0f, 2.0f, 3.0f, 9.0f });
    }

    SECTION("A texture entry writes nothing to the block")
    {
        const MaterialProperties properties = MaterialProperties().SetTexture("Albedo", TextureHandle{ 5, 1 });

        std::array<float, 4> block{};
        WriteMaterialBlock(std::as_writable_bytes(std::span(block)), materialBytes, shader, properties.Entries(), properties.Bytes());
        CHECK(block == materialValues);
    }

    SECTION("Only properties that write a value need a block of their own")
    {
        CHECK(SetsValues(MaterialProperties().Set("Amount", 1.0f).Entries(), shader));
        CHECK_FALSE(SetsValues(MaterialProperties().SetTexture("Albedo", TextureHandle{ 5, 1 }).Entries(), shader));
        CHECK_FALSE(SetsValues(MaterialProperties().Set("Missing", 1.0f).Entries(), shader));
        CHECK_FALSE(SetsValues(MaterialProperties().Set("Color", glm::vec4(0.0f)).Entries(), shader));

        ShaderInterface noBlock;
        noBlock.Properties.emplace(PropertyId("Amount"), ShaderPropertyInfo{ .Offset = 0, .Size = 4, .Name = "Amount" });
        CHECK_FALSE(SetsValues(MaterialProperties().Set("Amount", 1.0f).Entries(), noBlock));
    }

    SECTION("A texture is the draw's, else the material's, else none")
    {
        const TextureHandle fromMaterial{ 3, 1 };
        const TextureHandle fromDraw{ 4, 1 };

        MaterialTextures textures;
        textures.emplace(PropertyId("Albedo"), fromMaterial);

        const MaterialProperties properties = MaterialProperties().SetTexture("Albedo", fromDraw);

        CHECK(DrawTexture(PropertyId("Albedo"), textures, properties.Entries()) == fromDraw);
        CHECK(DrawTexture(PropertyId("Albedo"), textures, {}) == fromMaterial);
        CHECK_FALSE(DrawTexture(PropertyId("Normal"), textures, properties.Entries()));
    }

    SECTION("A frame's draws with properties are counted")
    {
        FramePacket packet;
        FrameRecorder frame(packet);

        const MaterialPropertiesId values = frame.AddProperties(MaterialProperties().Set("Amount", 1.0f));
        const MaterialPropertiesId texture = frame.AddProperties(MaterialProperties().SetTexture("Albedo", TextureHandle{ 5, 1 }));

        PassBuilder pass = frame.AddPass({});
        pass.Draw(DrawItem{ .Material = kMaterial });
        pass.Draw(DrawItem{ .Material = kMaterial, .Properties = values });
        frame.AddPass({}).Draw(DrawItem{ .Material = kMaterial, .Properties = texture });
        frame.AddPass({}).Draw(DrawItem{ .Material = kMaterial, .Properties = values });

        FakeShading shading;
        shading.Interface = shader;

        const MaterialPropertiesBudget budget = CountMaterialProperties(packet, shading, 64);

        // Three draws have properties, with two texture slots each, and two of them write the block of 16 bytes, up to 64
        CHECK(budget.Draws == 3);
        CHECK(budget.Textures == 6);
        CHECK(budget.BlockBytes == 128);
    }
}
