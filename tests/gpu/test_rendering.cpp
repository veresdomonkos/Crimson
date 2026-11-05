#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <crimson/asset_manager/asset_manager.hpp>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;

namespace
{
    constexpr uint32_t kSize = 64;

    constexpr glm::vec4 kClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    constexpr glm::ivec3 kBlack(0, 0, 0);
    constexpr glm::ivec3 kRed(255, 0, 0);
    constexpr glm::ivec3 kGreen(0, 255, 0);

    constexpr std::string_view kFlatShader = R"(
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space2) { float4 Color; };

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(ViewProj, mul(GetObjectData().Transform, float4(position, 1.0)));
}

float4 PSMain() : SV_Target { return Color; }
)";

    constexpr std::string_view kTexturedShader = R"(
#include "crimson/bindings.hlsli"

Texture2D Albedo : register(t0, space2);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.ClipPosition = mul(ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target { return Albedo.Sample(LinearWrap, input.UV); }
)";

    // A rectangle in clip space
    std::string MakeQuad(float left, float bottom, float right, float top, float depth = 0.5f)
    {
        return fmt::format(
            "v {0} {1} {4}\nv {2} {1} {4}\nv {2} {3} {4}\nv {0} {3} {4}\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
            "f 1/1 2/2 3/3 4/4\n",
            left, bottom, right, top, depth);
    }

    std::string MakeMaterial(std::string_view color, std::string_view extra = "")
    {
        return fmt::format(R"({{ "shader": "flat.hlsl", "cull": "none", {} "properties": {{ "Color": {} }} }})", extra, color);
    }

    // A 1x1 blue BMP
    std::string MakeImage()
    {
        constexpr std::array<unsigned char, 58> kBytes = {
            'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
            40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            255, 0, 0, 0
        };
        return std::string(reinterpret_cast<const char*>(kBytes.data()), kBytes.size());
    }

    struct Fixture
    {
        TempFolder Project;
        OffscreenTarget Target{ kSize, kSize };

        Fixture()
        {
            Project.Write("flat.hlsl", kFlatShader);
            Project.Write("red.mat", MakeMaterial("[1, 0, 0, 1]"));
            Project.Write("green.mat", MakeMaterial("[0, 1, 0, 1]"));
        }

        [[nodiscard]] RenderPassInfo Pass() const
        {
            return { .Target = Target.Handle(), .ClearFlags = ClearFlags::Color | ClearFlags::Depth, .ClearColor = kClearColor };
        }

        Image RenderMesh(AssetManager& assets, const char* mesh, const char* material)
        {
            const Mesh* loaded = assets.LoadMesh(mesh);
            const MaterialHandle loadedMaterial = assets.LoadMaterial(material);
            REQUIRE(loaded);
            REQUIRE(loadedMaterial);

            RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(Pass()).Draw(*loaded, 0, loadedMaterial); });

            return Target.Read();
        }
    };
}

TEST_CASE_METHOD(Fixture, "A pass clears its target to the color it asks for", "[rendering]")
{
    RenderPassInfo pass = Pass();
    pass.ClearColor = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);

    RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(pass); });

    const glm::ivec4 pixel = Target.Read().At(0, 0);

    CAPTURE(pixel.r, pixel.g, pixel.b, pixel.a);
    CHECK(IsNear(pixel, glm::ivec3(64, 128, 191), 2));
    CHECK(pixel.a == 255);
}

// The two backends' clip spaces differ, the engine hides that, so this is what a backend can get wrong
TEST_CASE_METHOD(Fixture, "A mesh lands where its clip space position says, on every backend", "[rendering]")
{
    Project.Write("right.obj", MakeQuad(0.0f, -1.0f, 1.0f, 1.0f));
    Project.Write("bottom.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 0.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    SECTION("Left and right")
    {
        const Image image = RenderMesh(assets, "right.obj", "red.mat");
        CHECK(IsNear(image.AtClip(0.5f, 0.0f), kRed));
        CHECK(IsNear(image.AtClip(-0.5f, 0.0f), kBlack));
    }

    SECTION("Up and down")
    {
        const Image image = RenderMesh(assets, "bottom.obj", "red.mat");
        CHECK(IsNear(image.AtClip(0.0f, -0.5f), kRed));
        CHECK(IsNear(image.AtClip(0.0f, 0.5f), kBlack));
    }
}

TEST_CASE_METHOD(Fixture, "The nearer of two meshes wins, whichever is drawn first", "[rendering]")
{
    Project.Write("near.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.2f));
    Project.Write("far.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.8f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* near = assets.LoadMesh("near.obj");
    const Mesh* far = assets.LoadMesh("far.obj");
    REQUIRE(near);
    REQUIRE(far);

    const MaterialHandle red = assets.LoadMaterial("red.mat");
    const MaterialHandle green = assets.LoadMaterial("green.mat");

    SECTION("Near one sorts first")
    {
        RenderOneFrame([&](FrameContext& frame)
        {
            RenderPass& pass = frame.BeginRenderPass(Pass());
            pass.Draw(*near, 0, red);
            pass.Draw(*far, 0, green);
        });

        CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kRed));
    }

    SECTION("Near one sorts last")
    {
        RenderOneFrame([&](FrameContext& frame)
        {
            RenderPass& pass = frame.BeginRenderPass(Pass());
            pass.Draw(*near, 0, green);
            pass.Draw(*far, 0, red);
        });

        CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kGreen));
    }
}

TEST_CASE_METHOD(Fixture, "An object's transform moves it", "[rendering]")
{
    Project.Write("small.obj", MakeQuad(-0.25f, -0.25f, 0.25f, 0.25f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const Mesh* mesh = assets.LoadMesh("small.obj");
    REQUIRE(mesh);
    const MaterialHandle red = assets.LoadMaterial("red.mat");

    RenderOneFrame([&](FrameContext& frame)
    {
        const ObjectId object = frame.AddObject(glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.0f)));
        frame.BeginRenderPass(Pass()).Draw(*mesh, object, red);
    });

    const Image image = Target.Read();
    CHECK(IsNear(image.AtClip(0.5f, 0.5f), kRed));
    CHECK(IsNear(image.AtClip(0.0f, 0.0f), kBlack));
}

TEST_CASE_METHOD(Fixture, "Materials keep their own properties", "[rendering]")
{
    Project.Write("left.obj", MakeQuad(-1.0f, -1.0f, 0.0f, 1.0f));
    Project.Write("right.obj", MakeQuad(0.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* left = assets.LoadMesh("left.obj");
    const Mesh* right = assets.LoadMesh("right.obj");
    REQUIRE(left);
    REQUIRE(right);

    const MaterialHandle red = assets.LoadMaterial("red.mat");
    const MaterialHandle green = assets.LoadMaterial("green.mat");

    const auto render = [&]
    {
        RenderOneFrame([&](FrameContext& frame)
        {
            RenderPass& pass = frame.BeginRenderPass(Pass());
            pass.Draw(*left, 0, red);
            pass.Draw(*right, 0, green);
        });

        return Target.Read();
    };

    SECTION("As loaded")
    {
        const Image image = render();
        CHECK(IsNear(image.AtClip(-0.5f, 0.0f), kRed));
        CHECK(IsNear(image.AtClip(0.5f, 0.0f), kGreen));
    }

    SECTION("Changing one from code leaves the other")
    {
        Gpu().GPUResources->SetMaterialPropertyByName(red, "Color", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

        const Image image = render();
        CHECK(IsNear(image.AtClip(-0.5f, 0.0f), glm::ivec3(0, 0, 255)));
        CHECK(IsNear(image.AtClip(0.5f, 0.0f), kGreen));
    }
}

TEST_CASE_METHOD(Fixture, "A material samples its texture", "[rendering]")
{
    Project.Write("textured.hlsl", kTexturedShader);
    Project.Write("image.bmp", MakeImage());
    Project.Write("textured.mat", R"({ "shader": "textured.hlsl", "cull": "none", "textures": { "Albedo": "image.bmp" } })");
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    CHECK(IsNear(RenderMesh(assets, "all.obj", "textured.mat").AtClip(0.0f, 0.0f), glm::ivec3(0, 0, 255)));
}

TEST_CASE_METHOD(Fixture, "A transparent material blends over what is behind it", "[rendering]")
{
    Project.Write("half.mat", MakeMaterial("[0, 1, 0, 0.5]", R"("renderMode": "transparent",)"));
    Project.Write("back.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.8f));
    Project.Write("front.obj", MakeQuad(-1.0f, -1.0f, 0.0f, 1.0f, 0.2f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* back = assets.LoadMesh("back.obj");
    const Mesh* front = assets.LoadMesh("front.obj");
    REQUIRE(back);
    REQUIRE(front);

    const MaterialHandle red = assets.LoadMaterial("red.mat");
    const MaterialHandle half = assets.LoadMaterial("half.mat");

    RenderOneFrame([&](FrameContext& frame)
    {
        RenderPass& pass = frame.BeginRenderPass(Pass());

        // In the wrong order on purpose: opaque draws have to go first anyway
        pass.Draw(*front, 0, half);
        pass.Draw(*back, 0, red);
    });

    const Image image = Target.Read();

    CHECK(IsNear(image.AtClip(-0.5f, 0.0f), glm::ivec3(127, 127, 0), 4));
    CHECK(IsNear(image.AtClip(0.5f, 0.0f), kRed));
}

TEST_CASE_METHOD(Fixture, "Passes can read what an earlier pass of the frame rendered", "[rendering]")
{
    Project.Write("textured.hlsl", kTexturedShader);
    Project.Write("textured.mat", R"({ "shader": "textured.hlsl", "cull": "none" })");
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const Mesh* mesh = assets.LoadMesh("all.obj");
    REQUIRE(mesh);

    const OffscreenTarget source(kSize, kSize);

    const MaterialHandle textured = assets.LoadMaterial("textured.mat");
    REQUIRE(textured);
    Gpu().GPUResources->SetMaterialTexture(textured, "Albedo", Gpu().GPUResources->GetColorAttachment(source.Handle(), 0));

    RenderOneFrame([&](FrameContext& frame)
    {
        frame.BeginRenderPass({ .Target = source.Handle(), .ClearFlags = ClearFlags::Color | ClearFlags::Depth, .ClearColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) });
        frame.BeginRenderPass(Pass()).Draw(*mesh, 0, textured);
    });

    CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kGreen));
}

TEST_CASE_METHOD(Fixture, "Assets can be loaded and freed again and again", "[rendering]")
{
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    for (int i = 0; i < 5; ++i)
    {
        AssetManager assets(*Gpu().GPUResources, Project.Path());
        CHECK(IsNear(RenderMesh(assets, "all.obj", "red.mat").AtClip(0.0f, 0.0f), kRed));
    }
}

TEST_CASE("The back buffer survives a window resize", "[rendering]")
{
    Fixture fixture;

    GpuWindow().SetSize(400, 300);
    RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(fixture.Pass()); });

    GpuWindow().SetSize(320, 240);
    RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(fixture.Pass()); });

    CHECK(IsNear(fixture.Target.Read().At(0, 0), kBlack));
}

TEST_CASE_METHOD(Fixture, "Shaders get the time and the time since the frame before", "[rendering]")
{
    // Red when some time has passed since the frame before, green when the time is at least that much
    Project.Write("timed.hlsl", R"(
#include "crimson/bindings.hlsli"

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(ViewProj, mul(GetObjectData().Transform, float4(position, 1.0)));
}

float4 PSMain() : SV_Target { return float4(DeltaTime >= 0.02 ? 1.0 : 0.0, Time >= DeltaTime ? 1.0 : 0.0, 0.0, 1.0); }
)");
    Project.Write("timed.mat", R"({ "shader": "timed.hlsl", "cull": "none" })");
    Project.Write("full.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    RenderMesh(assets, "full.obj", "timed.mat");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const Image image = RenderMesh(assets, "full.obj", "timed.mat");

    CHECK(IsNear(image.AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 0)));
}

TEST_CASE_METHOD(Fixture, "Going from an object into the world and back gives the same point", "[rendering]")
{
    // The quad covers the target whatever the transform is, which only goes into the helpers. Red means each
    // vertex came back to where it started, and had moved in the world.
    Project.Write("round_trip.hlsl", R"(
#include "crimson/transforms.hlsli"

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float Correct : CORRECT;
};

VertexOutput VSMain(float3 position : POSITION)
{
    const float3 world = ObjectToWorldPosition(position);
    const float3 direction = float3(0.3, -0.4, 0.5);

    const bool positionBack = length(WorldToObjectPosition(world) - position) < 0.001;
    const bool directionBack = length(WorldToObjectDirection(ObjectToWorldDirection(direction)) - direction) < 0.001;
    const bool moved = length(world - position) > 0.1;

    VertexOutput output;
    output.ClipPosition = float4(position, 1.0);
    output.Correct = positionBack && directionBack && moved ? 1.0 : 0.0;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target { return float4(input.Correct, 0.0, 0.0, 1.0); }
)");
    Project.Write("round_trip.mat", R"({ "shader": "round_trip.hlsl", "cull": "none" })");
    Project.Write("full.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const Mesh* mesh = assets.LoadMesh("full.obj");
    const MaterialHandle material = assets.LoadMaterial("round_trip.mat");
    REQUIRE(mesh);
    REQUIRE(material);

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, -3.0f));
    transform = glm::rotate(transform, 0.7f, glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)));
    transform = glm::scale(transform, glm::vec3(2.0f, 0.5f, 3.0f));

    RenderOneFrame([&](FrameContext& frame)
    {
        const ObjectId object = frame.AddObject(transform);
        frame.BeginRenderPass(Pass()).Draw(*mesh, object, material);
    });

    CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kRed));
}
