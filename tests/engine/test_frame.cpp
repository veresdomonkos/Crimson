#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <crimson/renderer/draw_sort.hpp>
#include <crimson/renderer/frame_context.hpp>
#include <crimson/renderer/frame_data.hpp>

using namespace crimson;

namespace
{
    constexpr RenderTargetHandle kBackBuffer{ 7, 1 };
    constexpr MaterialHandle kMaterial{ 1, 1 };
    constexpr MaterialHandle kOverride{ 2, 1 };

    struct FrameFixture
    {
        Frame Data;

        FrameFixture()
        {
            Data.Reset();
            Data.Init(kBackBuffer, true);
        }
    };

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

    DrawInfo MakeDraw(uint32_t material, uint32_t vertexBuffer = 0)
    {
        return DrawInfo{ .VertexBuffer = VertexBufferHandle{ vertexBuffer, 1 }, .Material = MaterialHandle{ material, 1 } };
    }

    std::vector<uint32_t> DrawOrder(const std::vector<DrawInfo>& draws, const FakeMaterials& materials)
    {
        std::vector<DrawSortEntry> order;
        BuildDrawOrder(std::span<const DrawInfo>(draws), materials, order);

        std::vector<uint32_t> indices;
        for (const DrawSortEntry& entry : order)
            indices.push_back(entry.Index);
        return indices;
    }
}

TEST_CASE("A pass without a target draws to the frame's default one", "[frame]")
{
    FrameFixture fixture;
    FrameContext frame = fixture.Data.CreateContext();

    const RenderPass& pass = frame.BeginRenderPass({});
    CHECK(pass.Info().Target == kBackBuffer);

    constexpr RenderTargetHandle other{ 9, 1 };
    const RenderPass& passWithTarget = frame.BeginRenderPass({ .Target = other });
    CHECK(passWithTarget.Info().Target == other);

    CHECK(fixture.Data.GetPasses().size() == 2);
}

TEST_CASE("A draw without a material is left out", "[frame]")
{
    RenderPass pass;

    pass.Draw(DrawInfo{ .Material = MaterialHandle::Invalid() });
    CHECK(pass.GetDraws().empty());

    pass.Draw(DrawInfo{ .Material = kMaterial });
    CHECK(pass.GetDraws().size() == 1);
}

TEST_CASE("Drawing a mesh makes one draw per submesh", "[frame]")
{
    Mesh mesh;
    mesh.Materials = { kMaterial, MaterialHandle::Invalid() };
    mesh.SubMeshes = { { .FirstIndex = 0, .IndexCount = 3, .MaterialSlot = 0 }, { .FirstIndex = 3, .IndexCount = 6, .MaterialSlot = 1 } };

    SECTION("Each with its own material, and the one that failed to load is skipped")
    {
        RenderPass pass;
        pass.Draw(mesh, 5);

        REQUIRE(pass.GetDraws().size() == 1);
        CHECK(pass.GetDraws()[0].Material == kMaterial);
        CHECK(pass.GetDraws()[0].IndexCount == 3);
        CHECK(pass.GetDraws()[0].Object == 5);
    }

    SECTION("An override replaces all of them, as a shadow pass needs")
    {
        RenderPass pass;
        pass.Draw(mesh, 0, kOverride);

        REQUIRE(pass.GetDraws().size() == 2);
        CHECK(pass.GetDraws()[0].Material == kOverride);
        CHECK(pass.GetDraws()[1].Material == kOverride);
    }
}

TEST_CASE("Object 0 is always the identity", "[frame]")
{
    FrameFixture fixture;
    FrameContext frame = fixture.Data.CreateContext();

    const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

    CHECK(frame.AddObject(moved) == 1);
    CHECK(frame.AddObject(moved) == 2);

    REQUIRE(fixture.Data.GetObjects().size() == 3);
    CHECK(fixture.Data.GetObjects()[0].Transform == glm::mat4(1.0f));
    CHECK(fixture.Data.GetObjects()[1].Transform == moved);
}

TEST_CASE("Resetting a frame leaves it empty for the next one", "[frame]")
{
    FrameFixture fixture;

    {
        FrameContext frame = fixture.Data.CreateContext();
        frame.BeginRenderPass({}).Draw(DrawInfo{ .Material = kMaterial });
        frame.AddRawPass({ .Callback = [](const NativeFrameHandles&) {} });
        frame.AddObject(glm::mat4(1.0f));
    }

    fixture.Data.Reset();

    CHECK(fixture.Data.GetPasses().empty());
    CHECK(fixture.Data.GetObjects().size() == 1);

    SECTION("A reused pass doesn't keep the last frame's draws")
    {
        fixture.Data.Init(kBackBuffer, true);
        FrameContext frame = fixture.Data.CreateContext();
        CHECK(frame.BeginRenderPass({}).GetDraws().empty());
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
        const std::vector<DrawInfo> draws = { MakeDraw(0), MakeDraw(1), MakeDraw(2) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 1, 0, 2 });
    }

    SECTION("Then by material, then by vertex buffer")
    {
        const std::vector<DrawInfo> draws = { MakeDraw(2, 1), MakeDraw(0, 5), MakeDraw(2, 0), MakeDraw(0, 1) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 3, 1, 2, 0 });
    }

    SECTION("Identical draws keep their order")
    {
        const std::vector<DrawInfo> draws = { MakeDraw(0), MakeDraw(0), MakeDraw(0) };
        CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 0, 1, 2 });
    }
}

TEST_CASE("Transparent draws come after opaque ones, in the order they were submitted", "[draw sort]")
{
    FakeMaterials materials;
    materials.Materials = {
        { ShaderHandle{ 3, 1 }, {} },
        { ShaderHandle{ 1, 1 }, { .RenderMode = MaterialRenderMode::Transparent } },
        { ShaderHandle{ 2, 1 }, { .RenderMode = MaterialRenderMode::Transparent } }
    };

    const std::vector<DrawInfo> draws = { MakeDraw(2), MakeDraw(1), MakeDraw(0), MakeDraw(2) };

    CHECK(DrawOrder(draws, materials) == std::vector<uint32_t>{ 2, 0, 1, 3 });
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
    data.Build(FrameLightingData{}, FrameTime{ 2.5f, 0.125f });

    CHECK(data.Block.Time == 2.5f);
    CHECK(data.Block.DeltaTime == 0.125f);
}
