#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <crimson/renderer/draw_sort.hpp>
#include <crimson/renderer/frame_gpu_data.hpp>
#include <crimson/renderer/frame_recorder.hpp>
#include <crimson/renderer/frame_validation.hpp>

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

    std::vector<uint32_t> DrawOrder(const std::vector<DrawItem>& draws, const FakeMaterials& materials)
    {
        std::vector<DrawSortEntry> order;
        BuildDrawOrder(std::span<const DrawItem>(draws), materials, order);

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
    mesh.Materials = { kMaterial, MaterialHandle::Invalid() };
    mesh.SubMeshes = { { .FirstIndex = 0, .IndexCount = 3, .MaterialSlot = 0 }, { .FirstIndex = 3, .IndexCount = 6, .MaterialSlot = 1 } };

    FramePacket packet;
    FrameRecorder frame(packet);
    PassBuilder pass = frame.AddPass({});

    SECTION("Each with its own material, and the one that failed to load is skipped")
    {
        const ObjectId object = frame.AddObject(glm::mat4(1.0f));
        pass.DrawMesh(mesh, object);

        REQUIRE(pass.GetDraws().size() == 1);
        CHECK(pass.GetDraws()[0].Material == kMaterial);
        CHECK(pass.GetDraws()[0].IndexCount == 3);
        CHECK(pass.GetDraws()[0].Object == object);
        CHECK(pass.GetDraws()[0].Variant == ShaderVariant::Main);
    }

    SECTION("An override replaces all of them")
    {
        pass.DrawMesh(mesh, 0, { .MaterialOverride = kOverride });

        REQUIRE(pass.GetDraws().size() == 2);
        CHECK(pass.GetDraws()[0].Material == kOverride);
        CHECK(pass.GetDraws()[1].Material == kOverride);
    }

    SECTION("A depth pass keeps each one's own material, in its depth version")
    {
        pass.DrawMesh(mesh, 0, { .Variant = ShaderVariant::Depth });

        REQUIRE(pass.GetDraws().size() == 1);
        CHECK(pass.GetDraws()[0].Material == kMaterial);
        CHECK(pass.GetDraws()[0].Variant == ShaderVariant::Depth);
    }
}

TEST_CASE("A native pass keeps its callback and has no draws", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    bool called = false;
    frame.AddNativePass({ .Name = "UI" }, [&called](const NativeContext&) { called = true; });

    REQUIRE(packet.GetPasses().size() == 1);
    const PassRecord& pass = packet.GetPasses()[0];
    REQUIRE(pass.IsNative());
    CHECK(pass.GetDraws().empty());

    pass.GetNativeFn()(NativeContext{});
    CHECK(called);
}

TEST_CASE("Object 0 is always the identity", "[frame]")
{
    FramePacket packet;
    FrameRecorder frame(packet);

    const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

    CHECK(frame.AddObject(moved) == 1);
    CHECK(frame.AddObject(moved) == 2);

    REQUIRE(packet.GetObjects().size() == 3);
    CHECK(packet.GetObjects()[0].Transform == glm::mat4(1.0f));
    CHECK(packet.GetObjects()[1].Transform == moved);
}

TEST_CASE("Resetting a packet leaves it empty for the next frame", "[frame]")
{
    FramePacket packet;

    {
        FrameRecorder frame(packet);
        frame.AddPass({}).Draw(DrawItem{ .Material = kMaterial });
        frame.AddNativePass({}, [](const NativeContext&) {});
        frame.AddObject(glm::mat4(1.0f));
        frame.Lighting().Lights.push_back({});
    }

    packet.Reset();

    CHECK(packet.GetPasses().empty());
    CHECK(packet.GetObjects().size() == 1);
    CHECK(packet.GetLighting().Lights.empty());

    SECTION("A reused pass doesn't keep the last frame's draws or callback")
    {
        FrameRecorder frame(packet);
        CHECK(frame.AddPass({}).GetDraws().empty());

        frame.AddPass({});
        CHECK_FALSE(packet.GetPasses()[1].IsNative());
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
    mesh.Materials = { kMaterial, kOverride };
    mesh.SubMeshes = { { .MaterialSlot = 0 }, { .MaterialSlot = 1 } };
    mesh.Bounds = { glm::vec3(-1.0f), glm::vec3(3.0f) }; // centered on (1, 1, 1)

    FramePacket packet;
    FrameRecorder frame(packet);

    const ObjectId object = frame.AddObject(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f)));
    PassBuilder pass = frame.AddPass({ .View = { .CameraPosition = glm::vec3(1.0f, 1.0f, 2.0f) } });
    pass.DrawMesh(mesh, object);

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
    CHECK(views[passes[1].GetView()].View.ViewProj == camera.ViewProj);
    CHECK(views[passes[1].GetView()].View.CameraPosition == camera.CameraPosition);
    CHECK_FALSE(views[passes[1].GetView()].Viewport);

    REQUIRE(views[extra].Viewport);
    CHECK(views[extra].Viewport->X == tile.X);
    CHECK(views[extra].Viewport->Y == tile.Y);
    CHECK(views[extra].Viewport->Width == tile.Width);
    CHECK(views[extra].Viewport->Height == tile.Height);

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
    mesh.Materials = { kMaterial };
    mesh.SubMeshes = { { .MaterialSlot = 0 } };
    mesh.Bounds = { glm::vec3(-1.0f), glm::vec3(1.0f) };

    FramePacket packet;
    FrameRecorder frame(packet);

    PassBuilder pass = frame.AddPass({ .View = { .CameraPosition = glm::vec3(0.0f, 0.0f, 2.0f) } });
    const ViewId far = frame.AddView({ .CameraPosition = glm::vec3(0.0f, 0.0f, 5.0f) });

    pass.DrawMesh(mesh);
    pass.DrawMesh(mesh, 0, { .View = far });

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
