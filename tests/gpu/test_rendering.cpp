#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/mesh_asset.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>


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

cbuffer MaterialBlock : register(b0, space1) { float4 Color; };

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(position, 1.0)));
}

float4 PSMain() : SV_Target { return Color; }
)";

    constexpr std::string_view kTexturedShader = R"(
#include "crimson/bindings.hlsli"

Texture2D Albedo : register(t0, space1);

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
    output.ClipPosition = mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target { return Albedo.Sample(LinearWrap, input.UV); }
)";

    // The same, but the texture goes through a function, which is how a parallax or triplanar helper takes it
    constexpr std::string_view kTextureArgumentShader = R"(
#include "crimson/bindings.hlsli"

Texture2D Albedo : register(t0, space1);

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
    output.ClipPosition = mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.UV = input.UV;
    return output;
}

float4 Fetch(Texture2D map, float2 uv) { return map.SampleGrad(LinearWrap, uv, ddx(uv), ddy(uv)); }

float4 PSMain(VertexOutput input) : SV_Target { return Fetch(Albedo, input.UV); }
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

    // The same rectangle with a normal along +Z, wound the other way when facingAway, so that the camera sees its back
    std::string MakeQuadWithNormal(float left, float bottom, float right, float top, bool facingAway)
    {
        return fmt::format(
            "v {0} {1} 0.5\nv {2} {1} 0.5\nv {2} {3} 0.5\nv {0} {3} 0.5\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
            "f {4}\n",
            left, bottom, right, top, facingAway ? "1/1/1 4/4/1 3/3/1 2/2/1" : "1/1/1 2/2/1 3/3/1 4/4/1");
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

        [[nodiscard]] PassInfo Pass() const
        {
            return { .Target = Target.Handle(), .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = kClearColor } };
        }

        Image RenderMesh(AssetManager& assets, const char* mesh, const char* material)
        {
            const Mesh* loaded = assets.Get<MeshAsset>(mesh);
            const MaterialHandle loadedMaterial = *assets.Get<MaterialAsset>(material);
            REQUIRE(loaded);
            REQUIRE(loadedMaterial);

            RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(Pass()).DrawMesh(*loaded, {}, {}, { .MaterialOverride = loadedMaterial }); });

            return Target.Read();
        }
    };
}

TEST_CASE_METHOD(Fixture, "A pass clears its target to the color it asks for", "[rendering]")
{
    PassInfo pass = Pass();
    pass.Clear.Color = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);

    RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(pass); });

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

    const Mesh* near = assets.Get<MeshAsset>("near.obj");
    const Mesh* far = assets.Get<MeshAsset>("far.obj");
    REQUIRE(near);
    REQUIRE(far);

    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
    const MaterialHandle green = *assets.Get<MaterialAsset>("green.mat");

    SECTION("Near one sorts first")
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            PassBuilder pass = frame.AddPass(Pass());
            pass.DrawMesh(*near, {}, {}, { .MaterialOverride = red });
            pass.DrawMesh(*far, {}, {}, { .MaterialOverride = green });
        });

        CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kRed));
    }

    SECTION("Near one sorts last")
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            PassBuilder pass = frame.AddPass(Pass());
            pass.DrawMesh(*near, {}, {}, { .MaterialOverride = green });
            pass.DrawMesh(*far, {}, {}, { .MaterialOverride = red });
        });

        CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kGreen));
    }
}

TEST_CASE_METHOD(Fixture, "An object's transform moves it", "[rendering]")
{
    Project.Write("small.obj", MakeQuad(-0.25f, -0.25f, 0.25f, 0.25f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const Mesh* mesh = assets.Get<MeshAsset>("small.obj");
    REQUIRE(mesh);
    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");

    RenderOneFrame([&](FrameRecorder& frame)
    {
        const ObjectId object = frame.AddObject(glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.0f)));
        frame.AddPass(Pass()).DrawMesh(*mesh, {}, object, { .MaterialOverride = red });
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

    const Mesh* left = assets.Get<MeshAsset>("left.obj");
    const Mesh* right = assets.Get<MeshAsset>("right.obj");
    REQUIRE(left);
    REQUIRE(right);

    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
    const MaterialHandle green = *assets.Get<MaterialAsset>("green.mat");

    const auto render = [&]
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            PassBuilder pass = frame.AddPass(Pass());
            pass.DrawMesh(*left, {}, {}, { .MaterialOverride = red });
            pass.DrawMesh(*right, {}, {}, { .MaterialOverride = green });
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

TEST_CASE_METHOD(Fixture, "There can be more materials than one descriptor pool holds", "[rendering]")
{
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* mesh = assets.Get<MeshAsset>("all.obj");
    const ShaderHandle shader = *assets.Get<ShaderAsset>("flat.hlsl");
    REQUIRE(mesh);
    REQUIRE(shader);

    // A Vulkan pool holds 256 sets, so these take a few of them
    constexpr int kMaterials = 600;

    std::vector<MaterialHandle> materials;
    for (int i = 0; i < kMaterials; ++i)
    {
        const MaterialHandle material = Gpu().GPUResources->CreateMaterial(shader, { .Cull = CullMode::None });
        REQUIRE(material);
        materials.push_back(material);
    }

    Gpu().GPUResources->SetMaterialPropertyByName(materials.back(), "Color", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

    RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(Pass()).DrawMesh(*mesh, {}, {}, { .MaterialOverride = materials.back() }); });
    CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kGreen));

    for (const MaterialHandle material : materials)
        Gpu().GPUResources->DestroyMaterial(material);
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

TEST_CASE_METHOD(Fixture, "A texture passed to a function is sampled on every backend", "[rendering]")
{
    Project.Write("argument.hlsl", kTextureArgumentShader);
    Project.Write("image.bmp", MakeImage());
    Project.Write("argument.mat", R"({ "shader": "argument.hlsl", "cull": "none", "textures": { "Albedo": "image.bmp" } })");
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    CHECK(IsNear(RenderMesh(assets, "all.obj", "argument.mat").AtClip(0.0f, 0.0f), glm::ivec3(0, 0, 255)));
}

TEST_CASE_METHOD(Fixture, "A transparent material blends over what is behind it", "[rendering]")
{
    Project.Write("half.mat", MakeMaterial("[0, 1, 0, 0.5]", R"("renderMode": "transparent",)"));
    Project.Write("back.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.8f));
    Project.Write("front.obj", MakeQuad(-1.0f, -1.0f, 0.0f, 1.0f, 0.2f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* back = assets.Get<MeshAsset>("back.obj");
    const Mesh* front = assets.Get<MeshAsset>("front.obj");
    REQUIRE(back);
    REQUIRE(front);

    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
    const MaterialHandle half = *assets.Get<MaterialAsset>("half.mat");

    RenderOneFrame([&](FrameRecorder& frame)
    {
        PassBuilder pass = frame.AddPass(Pass());

        // In the wrong order on purpose: opaque draws have to go first anyway
        pass.DrawMesh(*front, {}, {}, { .MaterialOverride = half });
        pass.DrawMesh(*back, {}, {}, { .MaterialOverride = red });
    });

    const Image image = Target.Read();

    CHECK(IsNear(image.AtClip(-0.5f, 0.0f), glm::ivec3(127, 127, 0), 4));
    CHECK(IsNear(image.AtClip(0.5f, 0.0f), kRed));
}

TEST_CASE_METHOD(Fixture, "Overlapping transparent draws blend back to front, whatever order they come in", "[rendering]")
{
    Project.Write("half_red.mat", MakeMaterial("[1, 0, 0, 0.5]", R"("renderMode": "transparent",)"));
    Project.Write("half_green.mat", MakeMaterial("[0, 1, 0, 0.5]", R"("renderMode": "transparent",)"));
    Project.Write("back.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.8f));
    Project.Write("front.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.2f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* back = assets.Get<MeshAsset>("back.obj");
    const Mesh* front = assets.Get<MeshAsset>("front.obj");
    REQUIRE(back);
    REQUIRE(front);

    const MaterialHandle halfRed = *assets.Get<MaterialAsset>("half_red.mat");
    const MaterialHandle halfGreen = *assets.Get<MaterialAsset>("half_green.mat");

    const auto render = [&](bool frontFirst)
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            // The camera is at the origin, so the front quad is the nearer one
            PassBuilder pass = frame.AddPass(Pass());

            if (frontFirst)
                pass.DrawMesh(*front, {}, {}, { .MaterialOverride = halfGreen });

            pass.DrawMesh(*back, {}, {}, { .MaterialOverride = halfRed });

            if (!frontFirst)
                pass.DrawMesh(*front, {}, {}, { .MaterialOverride = halfGreen });
        });

        return Target.Read().AtClip(0.0f, 0.0f);
    };

    // Half of the red over the black clear color, then half of the green over that
    const glm::ivec3 expected(63, 127, 0);

    CHECK(IsNear(render(true), expected, 4));
    CHECK(IsNear(render(false), expected, 4));
}

TEST_CASE_METHOD(Fixture, "Passes can read what an earlier pass of the frame rendered", "[rendering]")
{
    Project.Write("textured.hlsl", kTexturedShader);
    Project.Write("textured.mat", R"({ "shader": "textured.hlsl", "cull": "none" })");
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const Mesh* mesh = assets.Get<MeshAsset>("all.obj");
    REQUIRE(mesh);

    const OffscreenTarget source(kSize, kSize);

    const MaterialHandle textured = *assets.Get<MaterialAsset>("textured.mat");
    REQUIRE(textured);
    Gpu().GPUResources->SetMaterialTexture(textured, "Albedo", Gpu().GPUResources->GetColorAttachment(source.Handle(), 0));

    RenderOneFrame([&](FrameRecorder& frame)
    {
        frame.AddPass({ .Target = source.Handle(), .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) } });
        frame.AddPass(Pass()).DrawMesh(*mesh, {}, {}, { .MaterialOverride = textured });
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
    RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(fixture.Pass()); });

    GpuWindow().SetSize(320, 240);
    RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(fixture.Pass()); });

    CHECK(IsNear(fixture.Target.Read().At(0, 0), kBlack));
}

TEST_CASE_METHOD(Fixture, "Shaders get the time and the time since the frame before", "[rendering]")
{
    // Red when some time has passed since the frame before, green when the time is at least that much
    Project.Write("timed.hlsl", R"(
#include "crimson/bindings.hlsli"

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(position, 1.0)));
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
    const Mesh* mesh = assets.Get<MeshAsset>("full.obj");
    const MaterialHandle material = *assets.Get<MaterialAsset>("round_trip.mat");
    REQUIRE(mesh);
    REQUIRE(material);

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, -3.0f));
    transform = glm::rotate(transform, 0.7f, glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)));
    transform = glm::scale(transform, glm::vec3(2.0f, 0.5f, 3.0f));

    RenderOneFrame([&](FrameRecorder& frame)
    {
        const ObjectId object = frame.AddObject(transform);
        frame.AddPass(Pass()).DrawMesh(*mesh, {}, object, { .MaterialOverride = material });
    });

    CHECK(IsNear(Target.Read().AtClip(0.0f, 0.0f), kRed));
}

// The view is the identity and nothing tone maps here, so a color comes out as it was written
TEST_CASE_METHOD(Fixture, "S_Unlit draws its color and texture, ignoring lights", "[rendering]")
{
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));
    Project.Write("back.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.8f));
    Project.Write("front.obj", MakeQuad(-1.0f, -1.0f, 0.0f, 1.0f, 0.2f));
    Project.Write("image.bmp", MakeImage());

    const auto writeMaterial = [&](const std::string& name, std::string_view color, std::string_view extra)
    {
        Project.Write(name, fmt::format(R"({{ "shader": "engine/shaders/S_Unlit.hlsl", {} "properties": {{ "Color": {} }} }})", extra, color));
    };

    constexpr std::array kCulls = { "none", "back", "front" };

    writeMaterial("color.mat", "[1, 0, 0, 1]", R"("cull": "none",)");
    writeMaterial("textured.mat", "[1, 1, 1, 1]", R"("cull": "none", "textures": { "ColorMap": "image.bmp" },)");
    writeMaterial("half.mat", "[0, 1, 0, 0.5]", R"("cull": "none", "renderMode": "transparent",)");

    for (const char* cull : kCulls)
        writeMaterial(fmt::format("cull_{}.mat", cull), "[1, 0, 0, 1]", fmt::format(R"("cull": "{}",)", cull));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    SECTION("Color")
    {
        CHECK(IsNear(RenderMesh(assets, "all.obj", "color.mat").AtClip(0.0f, 0.0f), kRed));
    }

    SECTION("Color times ColorMap")
    {
        CHECK(IsNear(RenderMesh(assets, "all.obj", "textured.mat").AtClip(0.0f, 0.0f), glm::ivec3(0, 0, 255)));
    }

    SECTION("Transparent blends with its alpha")
    {
        const Mesh* back = assets.Get<MeshAsset>("back.obj");
        const Mesh* front = assets.Get<MeshAsset>("front.obj");
        const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
        const MaterialHandle half = *assets.Get<MaterialAsset>("half.mat");
        REQUIRE(back);
        REQUIRE(front);

        RenderOneFrame([&](FrameRecorder& frame)
        {
            PassBuilder pass = frame.AddPass(Pass());
            pass.DrawMesh(*back, {}, {}, { .MaterialOverride = red });
            pass.DrawMesh(*front, {}, {}, { .MaterialOverride = half });
        });

        const Image image = Target.Read();
        CHECK(IsNear(image.AtClip(-0.5f, 0.0f), glm::ivec3(127, 127, 0), 4));
        CHECK(IsNear(image.AtClip(0.5f, 0.0f), kRed));
    }

    SECTION("Every cull mode")
    {
        for (const char* cull : kCulls)
        {
            const glm::ivec3 expected = std::string_view(cull) == "front" ? kBlack : kRed;
            CAPTURE(cull);
            CHECK(IsNear(RenderMesh(assets, "all.obj", fmt::format("cull_{}.mat", cull).c_str()).AtClip(0.0f, 0.0f), expected));
        }
    }
}

TEST_CASE_METHOD(Fixture, "S_DebugNormals shows the world space normal as a color", "[rendering]")
{
    Project.Write("front.obj", MakeQuadWithNormal(-1.0f, -1.0f, 1.0f, 1.0f, false));
    Project.Write("back.obj", MakeQuadWithNormal(-1.0f, -1.0f, 1.0f, 1.0f, true));
    Project.Write("normals.mat", R"({ "shader": "engine/shaders/S_DebugNormals.hlsl", "cull": "none" })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    SECTION("Front")
    {
        CHECK(IsNear(RenderMesh(assets, "front.obj", "normals.mat").AtClip(0.0f, 0.0f), glm::ivec3(128, 128, 255), 2));
    }

    // Tells whether SV_IsFrontFace reaches the shader the right way round on each backend
    SECTION("Back face, flipped towards the camera")
    {
        CHECK(IsNear(RenderMesh(assets, "back.obj", "normals.mat").AtClip(0.0f, 0.0f), glm::ivec3(128, 128, 0), 2));
    }
}

TEST_CASE_METHOD(Fixture, "S_DebugUV draws a checkerboard in UV space, scaled by Tiling", "[rendering]")
{
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    // 20 squares per UV unit times 0.1 leaves 2x2 squares over the quad
    Project.Write("uv.mat", R"({ "shader": "engine/shaders/S_DebugUV.hlsl", "cull": "none", "properties": { "Tiling": [0.1, 0.1] } })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const Image image = RenderMesh(assets, "all.obj", "uv.mat");

    // The OBJ loader flips V, so it counts down from the top of the quad
    CHECK(IsNear(image.AtClip(-0.5f, -0.5f), glm::ivec3(204), 2));
    CHECK(IsNear(image.AtClip(0.5f, 0.5f), glm::ivec3(204), 2));
    CHECK(IsNear(image.AtClip(0.5f, -0.5f), glm::ivec3(51), 2));
    CHECK(IsNear(image.AtClip(-0.5f, 0.5f), glm::ivec3(51), 2));
}

namespace
{
    // The corners of one quarter of the target in clip space, as bottom left, bottom right, top right, top left
    std::array<glm::vec3, 4> QuarterCorners(int quarter)
    {
        const float left = (quarter & 1) ? 0.0f : -1.0f;
        const float bottom = (quarter & 2) ? 0.0f : -1.0f;

        return { glm::vec3(left, bottom, 0.5f), glm::vec3(left + 1.0f, bottom, 0.5f),
                 glm::vec3(left + 1.0f, bottom + 1.0f, 0.5f), glm::vec3(left, bottom + 1.0f, 0.5f) };
    }

    glm::vec2 QuarterCenter(int quarter)
    {
        return { (quarter & 1) ? 0.5f : -0.5f, (quarter & 2) ? 0.5f : -0.5f };
    }
}

TEST_CASE_METHOD(Fixture, "A stream buffer updated every frame draws what it was last given", "[rendering]")
{
    GpuResourceManager& gpu = *Gpu().GPUResources;
    AssetManager assets(gpu, Project.Path());

    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
    REQUIRE(red);

    const std::array<glm::vec3, 4> firstQuarter = QuarterCorners(0);
    const VertexBufferHandle vertices = gpu.CreateVertexBuffer({ .Layout = { ShaderDataType::Float3 }, .Size = sizeof(firstQuarter), .Usage = BufferUsage::Stream }, nullptr);
    const IndexBufferHandle indices = gpu.CreateIndexBuffer({ .Size = 6 * sizeof(uint16_t), .Usage = BufferUsage::Stream, .Type = IndexType::UInt16 }, nullptr);
    REQUIRE(vertices);
    REQUIRE(indices);

    const auto draw = [&](std::uint32_t indexCount)
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            frame.AddPass(Pass()).Draw({ .VertexBuffer = vertices, .IndexBuffer = indices, .Material = red, .IndexCount = indexCount });
        });

        return Target.Read();
    };

    // Written once, and drawn by every frame below
    const std::array<uint16_t, 6> quad = { 0, 1, 2, 0, 2, 3 };
    gpu.UpdateIndexBuffer(indices, std::as_bytes(std::span(quad)));

    // More frames than there are copies of a buffer, each with the quad somewhere else
    for (int frame = 0; frame < 4; ++frame)
    {
        const std::array<glm::vec3, 4> corners = QuarterCorners(frame);
        gpu.UpdateVertexBuffer(vertices, std::as_bytes(std::span(corners)));

        const Image image = draw(6);

        for (int quarter = 0; quarter < 4; ++quarter)
        {
            const glm::vec2 at = QuarterCenter(quarter);

            CAPTURE(frame, quarter);
            CHECK(IsNear(image.AtClip(at.x, at.y), quarter == frame ? kRed : kBlack));
        }
    }

    // One triangle of the last quarter, the one under its diagonal
    const std::array<uint16_t, 3> triangle = { 0, 1, 2 };
    gpu.UpdateIndexBuffer(indices, std::as_bytes(std::span(triangle)));

    const Image half = draw(3);
    CHECK(IsNear(half.AtClip(0.75f, 0.25f), kRed));
    CHECK(IsNear(half.AtClip(0.25f, 0.75f), kBlack));

    // A static buffer can't change, so it draws what it was created with
    const VertexBufferHandle fixedVertices = gpu.CreateVertexBuffer({ .Layout = { ShaderDataType::Float3 }, .Size = sizeof(firstQuarter), .Usage = BufferUsage::Static }, firstQuarter.data());
    REQUIRE(fixedVertices);

    const std::array<glm::vec3, 4> elsewhere = QuarterCorners(3);
    gpu.UpdateVertexBuffer(fixedVertices, std::as_bytes(std::span(elsewhere)));
    CHECK(ExpectLoggedErrors("static buffer") == 1);

    gpu.UpdateIndexBuffer(indices, std::as_bytes(std::span(quad)));
    RenderOneFrame([&](FrameRecorder& frame)
    {
        frame.AddPass(Pass()).Draw({ .VertexBuffer = fixedVertices, .IndexBuffer = indices, .Material = red, .IndexCount = 6 });
    });

    const Image fixedImage = Target.Read();
    CHECK(IsNear(fixedImage.AtClip(-0.5f, -0.5f), kRed));
    CHECK(IsNear(fixedImage.AtClip(0.5f, 0.5f), kBlack));

    gpu.DestroyVertexBuffer(fixedVertices);
    gpu.DestroyVertexBuffer(vertices);
    gpu.DestroyIndexBuffer(indices);
}

namespace
{
    // The textured shader with no filtering, so each pixel shows the texel it lands in
    constexpr std::string_view kPointSampledShader = R"(
#include "crimson/bindings.hlsli"

Texture2D Albedo : register(t0, space1);

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
    output.ClipPosition = mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target { return Albedo.Sample(PointClamp, input.UV); }
)";
}

TEST_CASE_METHOD(Fixture, "Updating a region of a texture changes only that region", "[rendering]")
{
    Project.Write("point.hlsl", kPointSampledShader);
    Project.Write("point.mat", R"({ "shader": "point.hlsl", "cull": "none" })");
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    GpuResourceManager& gpu = *Gpu().GPUResources;
    AssetManager assets(gpu, Project.Path());

    const Mesh* mesh = assets.Get<MeshAsset>("all.obj");
    const MaterialHandle material = *assets.Get<MaterialAsset>("point.mat");
    REQUIRE(mesh);
    REQUIRE(material);

    // Four red texels, the first row being the bottom of the quad
    const std::array<unsigned char, 16> red = { 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255 };
    const TextureHandle texture = gpu.CreateTexture({ .Width = 2, .Height = 2 }, red.data());
    REQUIRE(texture);
    gpu.SetMaterialTexture(material, "Albedo", texture);

    const auto render = [&]
    {
        RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(Pass()).DrawMesh(*mesh, {}, {}, { .MaterialOverride = material }); });
        return Target.Read();
    };

    const Image before = render();
    CHECK(IsNear(before.AtClip(-0.5f, -0.5f), kRed));
    CHECK(IsNear(before.AtClip(0.5f, -0.5f), kRed));

    // The texel right of the first one
    const std::array<unsigned char, 4> green = { 0, 255, 0, 255 };
    gpu.UpdateTexture(texture, { 1, 0, 1, 1 }, green.data());

    const Image after = render();
    CHECK(IsNear(after.AtClip(0.5f, -0.5f), kGreen));
    CHECK(IsNear(after.AtClip(-0.5f, -0.5f), kRed));
    CHECK(IsNear(after.AtClip(-0.5f, 0.5f), kRed));
    CHECK(IsNear(after.AtClip(0.5f, 0.5f), kRed));

    // A region of several rows has them one after the other
    const std::array<unsigned char, 8> blue = { 0, 0, 255, 255, 0, 0, 255, 255 };
    gpu.UpdateTexture(texture, { 0, 0, 1, 2 }, blue.data());

    const glm::ivec3 kBlue(0, 0, 255);
    const Image column = render();
    CHECK(IsNear(column.AtClip(-0.5f, -0.5f), kBlue));
    CHECK(IsNear(column.AtClip(-0.5f, 0.5f), kBlue));
    CHECK(IsNear(column.AtClip(0.5f, -0.5f), kGreen));
    CHECK(IsNear(column.AtClip(0.5f, 0.5f), kRed));

    // What can't be updated is reported, and left as it was
    gpu.UpdateTexture(texture, { 1, 1, 2, 1 }, red.data());
    CHECK(ExpectLoggedErrors("outside the texture") == 1);

    const TextureHandle mipped = gpu.CreateTexture({ .Width = 2, .Height = 2, .MipLevels = 2 }, red.data());
    REQUIRE(mipped);
    gpu.UpdateTexture(mipped, { 0, 0, 1, 1 }, green.data());
    CHECK(ExpectLoggedErrors("mip levels") == 1);

    CHECK(IsNear(render().AtClip(0.5f, 0.5f), kRed));

    gpu.DestroyTexture(mipped);
    gpu.SetMaterialTexture(material, "Albedo", {});
    gpu.DestroyTexture(texture);
}

namespace
{
    // Draws the color from a vertex attribute of four bytes
    constexpr std::string_view kVertexColorShader = R"(
#include "crimson/bindings.hlsli"

struct VertexInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float4 Color : COLOR;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.ClipPosition = mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.Color = input.Color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target { return input.Color; }
)";

    struct ColoredVertex
    {
        glm::vec3 Position;
        std::array<unsigned char, 4> Color;
    };

    static_assert(sizeof(ColoredVertex) == 16);
}

TEST_CASE_METHOD(Fixture, "A draw's scissor and base vertex pick what it draws and where", "[rendering]")
{
    Project.Write("vertex_color.hlsl", kVertexColorShader);
    Project.Write("vertex_color.mat", R"({ "shader": "vertex_color.hlsl", "cull": "none" })");

    GpuResourceManager& gpu = *Gpu().GPUResources;
    AssetManager assets(gpu, Project.Path());

    const MaterialHandle material = *assets.Get<MaterialAsset>("vertex_color.mat");
    REQUIRE(material);

    // The left half of the target and then its right half, in one buffer
    constexpr std::array<unsigned char, 4> kLime = { 128, 255, 0, 255 };
    const std::array<ColoredVertex, 8> vertices = {
        ColoredVertex{ { -1.0f, -1.0f, 0.5f }, kLime }, ColoredVertex{ { 0.0f, -1.0f, 0.5f }, kLime },
        ColoredVertex{ { 0.0f, 1.0f, 0.5f }, kLime }, ColoredVertex{ { -1.0f, 1.0f, 0.5f }, kLime },
        ColoredVertex{ { 0.0f, -1.0f, 0.5f }, kLime }, ColoredVertex{ { 1.0f, -1.0f, 0.5f }, kLime },
        ColoredVertex{ { 1.0f, 1.0f, 0.5f }, kLime }, ColoredVertex{ { 0.0f, 1.0f, 0.5f }, kLime },
    };
    const std::array<uint16_t, 6> indices = { 0, 1, 2, 0, 2, 3 };

    const VertexBufferHandle vertexBuffer = gpu.CreateVertexBuffer(
        { .Layout = { ShaderDataType::Float3, ShaderDataType::UByte4Norm }, .Size = sizeof(vertices), .Usage = BufferUsage::Static }, vertices.data());
    const IndexBufferHandle indexBuffer = gpu.CreateIndexBuffer({ .Size = sizeof(indices), .Usage = BufferUsage::Static, .Type = IndexType::UInt16 }, indices.data());
    REQUIRE(vertexBuffer);
    REQUIRE(indexBuffer);

    const auto draw = [&](int32_t baseVertex, std::optional<RenderRect> scissor)
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            frame.AddPass(Pass()).Draw({ .VertexBuffer = vertexBuffer, .IndexBuffer = indexBuffer, .Material = material, .Scissor = scissor, .BaseVertex = baseVertex });
        });

        return Target.Read();
    };

    // The same four indices find the left quad or the right one, and the bytes arrive as the floats 0.5 and 1
    const Image left = draw(0, std::nullopt);
    CHECK(IsNear(left.AtClip(-0.5f, 0.0f), glm::ivec3(128, 255, 0), 2));
    CHECK(IsNear(left.AtClip(0.5f, 0.0f), kBlack));

    const Image right = draw(4, std::nullopt);
    CHECK(IsNear(right.AtClip(0.5f, 0.0f), glm::ivec3(128, 255, 0), 2));
    CHECK(IsNear(right.AtClip(-0.5f, 0.0f), kBlack));

    // The bottom left quarter, counting rows from the bottom of clip space like the viewport does
    const Image cut = draw(0, RenderRect{ 0, 0, kSize / 2, kSize / 2 });
    CHECK(IsNear(cut.AtClip(-0.5f, -0.5f), glm::ivec3(128, 255, 0), 2));
    CHECK(IsNear(cut.AtClip(-0.5f, 0.5f), kBlack));
    CHECK(IsNear(cut.AtClip(0.5f, -0.5f), kBlack));

    // A scissor outside what is drawn leaves nothing
    const Image nothing = draw(0, RenderRect{ kSize / 2, 0, kSize / 2, kSize });
    CHECK(IsNear(nothing.AtClip(-0.5f, 0.0f), kBlack));
    CHECK(IsNear(nothing.AtClip(0.5f, 0.0f), kBlack));

    gpu.DestroyVertexBuffer(vertexBuffer);
    gpu.DestroyIndexBuffer(indexBuffer);
}

TEST_CASE_METHOD(Fixture, "An overlay pass draws in recorded order, blended, without depth", "[rendering]")
{
    Project.Write("half_green.mat", MakeMaterial("[0, 1, 0, 0.5]"));
    Project.Write("near.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.1f));
    Project.Write("far.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.9f));

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    const Mesh* near = assets.Get<MeshAsset>("near.obj");
    const Mesh* far = assets.Get<MeshAsset>("far.obj");
    REQUIRE(near);
    REQUIRE(far);

    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
    const MaterialHandle halfGreen = *assets.Get<MaterialAsset>("half_green.mat");

    const auto render = [&](PassKind kind)
    {
        PassInfo info = Pass();
        info.Kind = kind;

        RenderOneFrame([&](FrameRecorder& frame)
        {
            // The nearer quad first, and the farther one, which depth would hide, after it
            PassBuilder pass = frame.AddPass(info);
            pass.DrawMesh(*near, {}, {}, { .MaterialOverride = red });
            pass.DrawMesh(*far, {}, {}, { .MaterialOverride = halfGreen });
        });

        return Target.Read().AtClip(0.0f, 0.0f);
    };

    // Half of the green over the red, even though it is farther away and its material is opaque
    CHECK(IsNear(render(PassKind::Overlay), glm::ivec3(127, 127, 0), 4));

    // A scene pass tests depth, so the nearer red one stays
    CHECK(IsNear(render(PassKind::Scene), kRed));
}

namespace
{
    // A texture and a color that multiplies it, which a draw can both replace
    constexpr std::string_view kTintedTextureShader = R"(
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space1) { float4 Tint; };
Texture2D Albedo : register(t0, space1);

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
    output.ClipPosition = mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target { return Albedo.Sample(LinearWrap, input.UV) * Tint; }
)";
}

TEST_CASE_METHOD(Fixture, "Material properties change one draw, not its material", "[rendering]")
{
    Project.Write("left.obj", MakeQuad(-1.0f, -1.0f, 0.0f, 1.0f));
    Project.Write("right.obj", MakeQuad(0.0f, -1.0f, 1.0f, 1.0f));

    GpuResourceManager& gpu = *Gpu().GPUResources;
    AssetManager assets(gpu, Project.Path());

    const Mesh* left = assets.Get<MeshAsset>("left.obj");
    const Mesh* right = assets.Get<MeshAsset>("right.obj");
    const MaterialHandle red = *assets.Get<MaterialAsset>("red.mat");
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(red);

    const std::array<unsigned char, 4> greenTexel = { 0, 255, 0, 255 };
    const TextureHandle greenTexture = gpu.CreateTexture({ .Width = 1, .Height = 1 }, greenTexel.data());
    REQUIRE(greenTexture);

    // The same material twice: plain on the left, and with its color set on the right
    const auto renderColors = [&](bool withProperties)
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            PassBuilder pass = frame.AddPass(Pass());
            pass.DrawMesh(*left, {}, {}, { .MaterialOverride = red });

            DrawOptions options = { .MaterialOverride = red };

            // Names the shader lacks are left out, without an error
            if (withProperties)
            {
                options.Properties = frame.AddProperties(MaterialProperties()
                    .Set("Color", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f))
                    .Set("NotInTheShader", 5.0f)
                    .SetTexture("AlsoNotInTheShader", greenTexture));
            }

            pass.DrawMesh(*right, {}, {}, options);
        });

        return Target.Read();
    };

    const Image withProperties = renderColors(true);
    CHECK(IsNear(withProperties.AtClip(-0.5f, 0.0f), kRed));
    CHECK(IsNear(withProperties.AtClip(0.5f, 0.0f), kGreen));

    // The material itself was left alone
    const Image without = renderColors(false);
    CHECK(IsNear(without.AtClip(-0.5f, 0.0f), kRed));
    CHECK(IsNear(without.AtClip(0.5f, 0.0f), kRed));

    // Textures: the blue image of the material, or the green one a draw brings
    Project.Write("textured.hlsl", kTintedTextureShader);
    Project.Write("image.bmp", MakeImage());
    Project.Write("textured.mat", R"({ "shader": "textured.hlsl", "cull": "none", "textures": { "Albedo": "image.bmp" }, "properties": { "Tint": [1, 1, 1, 1] } })");
    Project.Write("all.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    const Mesh* all = assets.Get<MeshAsset>("all.obj");
    const MaterialHandle textured = *assets.Get<MaterialAsset>("textured.mat");
    REQUIRE(all);
    REQUIRE(textured);

    const auto renderTextured = [&](const MaterialProperties& properties)
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            frame.AddPass(Pass()).DrawMesh(*all, {}, {}, { .MaterialOverride = textured, .Properties = frame.AddProperties(properties) });
        });

        return Target.Read().AtClip(0.0f, 0.0f);
    };

    CHECK(IsNear(renderTextured(MaterialProperties()), glm::ivec3(0, 0, 255)));
    CHECK(IsNear(renderTextured(MaterialProperties().SetTexture("Albedo", greenTexture)), kGreen));
    CHECK(IsNear(renderTextured(MaterialProperties().SetTexture("Albedo", greenTexture).Set("Tint", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))), glm::ivec3(0, 127, 0), 4));
    CHECK(IsNear(renderTextured(MaterialProperties()), glm::ivec3(0, 0, 255)));

    // Within one pass, the draw after one with its own texture has the material's again. An overlay keeps the order, so the
    // draw with properties comes first.
    PassInfo overlay = Pass();
    overlay.Kind = PassKind::Overlay;

    RenderOneFrame([&](FrameRecorder& frame)
    {
        PassBuilder pass = frame.AddPass(overlay);
        const MaterialPropertiesId green = frame.AddProperties(MaterialProperties().SetTexture("Albedo", greenTexture));

        pass.DrawMesh(*right, {}, {}, { .MaterialOverride = textured, .Properties = green });
        pass.DrawMesh(*left, {}, {}, { .MaterialOverride = textured });
    });

    const Image samePass = Target.Read();
    CHECK(IsNear(samePass.AtClip(0.5f, 0.0f), kGreen));
    CHECK(IsNear(samePass.AtClip(-0.5f, 0.0f), glm::ivec3(0, 0, 255)));

    gpu.DestroyTexture(greenTexture);
}

TEST_CASE_METHOD(Fixture, "Many draws with their own properties, frame after frame", "[rendering]")
{
    Project.Write("textured.hlsl", kTintedTextureShader);
    Project.Write("image.bmp", MakeImage());
    Project.Write("textured.mat", R"({ "shader": "textured.hlsl", "cull": "none", "textures": { "Albedo": "image.bmp" }, "properties": { "Tint": [1, 1, 1, 1] } })");
    Project.Write("cell.obj", MakeQuad(-1.0f, -1.0f, 1.0f, 1.0f));

    GpuResourceManager& gpu = *Gpu().GPUResources;
    AssetManager assets(gpu, Project.Path());

    const Mesh* cell = assets.Get<MeshAsset>("cell.obj");
    const MaterialHandle textured = *assets.Get<MaterialAsset>("textured.mat");
    REQUIRE(cell);
    REQUIRE(textured);

    // The material's image is blue, so a color other than blue shows that the draw's own texture and tint are both used
    const std::array<unsigned char, 4> whiteTexel = { 255, 255, 255, 255 };
    const TextureHandle white = gpu.CreateTexture({ .Width = 1, .Height = 1 }, whiteTexel.data());
    REQUIRE(white);

    // A grid of cells, each its own draw with its own tint: more than a pool of the first frame's size would hold
    constexpr int kColumns = 20;
    constexpr int kRows = 15;
    constexpr int kCells = kColumns * kRows;

    const auto tintOf = [](int index) { return glm::vec3(index % 3 == 0 ? 1.0f : 0.0f, index % 3 == 1 ? 1.0f : 0.0f, index % 3 == 2 ? 1.0f : 0.0f); };
    const auto centerOf = [](int index) { return glm::vec2(-1.0f + (static_cast<float>(index % kColumns) + 0.5f) * 2.0f / kColumns, -1.0f + (static_cast<float>(index / kColumns) + 0.5f) * 2.0f / kRows); };

    // Few draws first, then many, over more frames than there are frames in flight: every slot creates its pool and buffer, and
    // then replaces the ones it used with larger ones, and reuses those
    constexpr std::array<int, 4> kDrawCounts = { 10, 10, kCells, kCells };

    for (std::size_t frameNumber = 0; frameNumber < kDrawCounts.size(); ++frameNumber)
    {
        RenderOneFrame([&](FrameRecorder& frame)
        {
            PassBuilder pass = frame.AddPass(Pass());

            for (int index = 0; index < kDrawCounts[frameNumber]; ++index)
            {
                const glm::vec2 center = centerOf(index);
                const ObjectId object = frame.AddObject(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.4f / kColumns, 0.4f / kRows, 1.0f)));
                const MaterialPropertiesId properties = frame.AddProperties(MaterialProperties().Set("Tint", glm::vec4(tintOf(index), 1.0f)).SetTexture("Albedo", white));

                pass.DrawMesh(*cell, {}, object, { .MaterialOverride = textured, .Properties = properties });
            }
        });

        const Image image = Target.Read();

        for (const int index : { 0, 1, 2, 9, 17, 150, 299 })
        {
            if (index >= kDrawCounts[frameNumber])
                continue;

            const glm::vec2 center = centerOf(index);
            const glm::vec3 tint = tintOf(index);

            CAPTURE(frameNumber, index);
            CHECK(IsNear(image.AtClip(center.x, center.y), glm::ivec3(tint * 255.0f), 4));
        }
    }

    gpu.DestroyTexture(white);
}
