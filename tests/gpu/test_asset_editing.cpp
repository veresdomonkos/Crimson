#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;

namespace
{
    constexpr uint32_t kSize = 64;

    constexpr std::string_view kFullQuad =
        "v -1 -1 0.5\nv 1 -1 0.5\nv 1 1 0.5\nv -1 1 0.5\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n";

    // Shows a texture as it comes out of the sampler, with no lighting
    constexpr std::string_view kUnlitShader = R"(
#include "crimson/bindings.hlsli"

Texture2D Picture : register(t0, space1);

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

float4 PSMain(VertexOutput input) : SV_Target
{
    return Picture.Sample(PointClamp, input.UV);
}
)";

    // A lit material also reflects a little ambient light, so even pure red has some green and blue
    constexpr int kLitTolerance = 12;

    // Every other pixel black and white, so it averages to gray when it is small enough
    std::string MakeCheckerboard(uint32_t size)
    {
        std::vector<Rgb> pixels(size * size);

        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
                pixels[y * size + x] = (x + y) % 2 == 0 ? Rgb{ 255, 255, 255 } : Rgb{ 0, 0, 0 };

        return MakeBmp(size, size, pixels);
    }

    // Lit materials sample the shadow atlas, which comes from a scene renderer
    SceneRenderer MakeSceneRenderer()
    {
        return { *Gpu().GPUResources, *Gpu().Renderer, ShadowAtlasSettings{ .AtlasSize = 256, .TileSize = 256 } };
    }

    struct Fixture
    {
        TempFolder Project;
        OffscreenTarget Target{ kSize, kSize };
        SceneRenderer Shadows = MakeSceneRenderer();

        // Full ambient light, so a lit material shows exactly what it is
        FrameLighting Lighting;

        Fixture()
        {
            Lighting.AmbientColor = glm::vec3(1.0f);

            Project.Write("quad.obj", kFullQuad);
            Project.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "cull": "none", "properties": { "Color": [1, 0, 0, 1] } })");
        }

        Image Draw(AssetManager& assets, const OffscreenTarget& target, const char* material = "mine.mat")
        {
            const Mesh* mesh = assets.Load<AssetType::Mesh>("quad.obj");
            const MaterialHandle handle = assets.Load<AssetType::Material>(material);
            REQUIRE(mesh);
            REQUIRE(handle);

            // In front of the quad, which faces +Z, for the lighting to see it from the front
            const PassInfo pass{ .Target = target.Handle(), .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth }, .View = { .CameraPosition = glm::vec3(0.0f, 0.0f, 2.0f) } };

            RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(pass).DrawMesh(*mesh, 0, { .MaterialOverride = handle }); }, Lighting);

            return target.Read();
        }
    };
}

TEST_CASE_METHOD(Fixture, "Editing a material shows on the next frame", "[asset editing]")
{
    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const MaterialID id = assets.Find<AssetType::Material>("mine.mat");

    REQUIRE(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(255, 0, 0), kLitTolerance));

    MaterialData data = *assets.GetMaterialData(id);

    SECTION("A property")
    {
        data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };

        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        CHECK(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(0, 255, 0), kLitTolerance));
    }

    SECTION("The settings: a mesh facing away disappears when back faces are culled")
    {
        Project.Write("away.obj", "v -1 1 0.5\nv 1 1 0.5\nv 1 -1 0.5\nv -1 -1 0.5\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n");
        assets.Refresh();

        const auto drawAway = [&]
        {
            const Mesh* mesh = assets.Load<AssetType::Mesh>("away.obj");
            REQUIRE(mesh);

            const MaterialHandle handle = assets.Load(id);
            const PassInfo pass{ .Target = Target.Handle(), .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth }, .View = { .CameraPosition = glm::vec3(0.0f, 0.0f, 2.0f) } };

            RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(pass).DrawMesh(*mesh, 0, { .MaterialOverride = handle }); }, Lighting);

            return Target.Read().AtClip(0.0f, 0.0f);
        };

        const glm::ivec4 noCulling = drawAway();

        data.Settings.Cull = CullMode::Back;
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        const glm::ivec4 backCulling = drawAway();

        data.Settings.Cull = CullMode::Front;
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        const glm::ivec4 frontCulling = drawAway();

        CHECK(IsNear(noCulling, glm::ivec3(255, 0, 0), kLitTolerance));

        // Which of the two hides it depends on the winding, but it can only be one of them
        CHECK(IsNear(backCulling, glm::ivec3(0, 0, 0)) != IsNear(frontCulling, glm::ivec3(0, 0, 0)));
    }

    SECTION("Another shader, which makes a new material")
    {
        Project.Write("green.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return mul(GetView().ViewProj, mul(GetObjectData().Transform, float4(position, 1.0))); }
float4 PSMain() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }
)");
        assets.Refresh();

        data.Shader = assets.Find<AssetType::Shader>("green.hlsl");
        data.Properties.clear();

        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Replaced);
        CHECK(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(0, 255, 0), kLitTolerance));
    }

    SECTION("Reverting to the file brings it back")
    {
        data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        REQUIRE(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(0, 255, 0), kLitTolerance));

        const auto saved = assets.ReadMaterialFile(id);
        REQUIRE(saved);
        REQUIRE(assets.ApplyMaterial(id, *saved) == MaterialApplyResult::Updated);
        CHECK(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(255, 0, 0), kLitTolerance));
    }
}

TEST_CASE_METHOD(Fixture, "A material's color map multiplies its color", "[asset editing]")
{
    Project.Write("blue.bmp", MakeBmp(1, 1, { Rgb{ 0, 0, 255 } }));
    Project.Write("white.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "cull": "none", "properties": { "Color": [1, 1, 1, 1] } })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const MaterialID id = assets.Find<AssetType::Material>("white.mat");

    REQUIRE(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 255), kLitTolerance));

    MaterialData data = *assets.GetMaterialData(id);
    data.Textures["ColorMap"] = assets.Find<AssetType::Texture>("blue.bmp");
    REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);

    CHECK(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(0, 0, 255), kLitTolerance));

    SECTION("Clearing it goes back to white")
    {
        data.Textures.clear();
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);

        CHECK(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 255), kLitTolerance));
    }

    SECTION("Deleting the texture goes back to white too")
    {
        REQUIRE(assets.Delete(assets.Find<AssetType::Texture>("blue.bmp")));

        CHECK(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 255), kLitTolerance));
    }
}

TEST_CASE("Mipmaps keep a shrunken texture from flickering", "[asset editing]")
{
    TempFolder project;
    project.Write("quad.obj", kFullQuad);
    project.Write("checker.bmp", MakeCheckerboard(256));
    project.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "cull": "none", "properties": { "Color": [1, 1, 1, 1] }, "textures": { "ColorMap": "checker.bmp" } })");

    // A size that doesn't divide the texture evenly, so without mips the samples land on different mixes of the two colors
    const OffscreenTarget target(37, 37);
    const SceneRenderer shadows = MakeSceneRenderer();

    AssetManager assets(*Gpu().GPUResources, project.Path());
    const TextureID texture = assets.Find<AssetType::Texture>("checker.bmp");
    FrameLighting lighting;
    lighting.AmbientColor = glm::vec3(1.0f);

    const auto spread = [&]
    {
        const Mesh* mesh = assets.Load<AssetType::Mesh>("quad.obj");
        const MaterialHandle material = assets.Load<AssetType::Material>("mine.mat");
        REQUIRE(mesh);
        REQUIRE(material);

        const PassInfo pass{ .Target = target.Handle(), .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth } };
        RenderOneFrame([&](FrameRecorder& frame) { frame.AddPass(pass).DrawMesh(*mesh, 0, { .MaterialOverride = material }); }, lighting);

        const Image image = target.Read();
        int lowest = 255;
        int highest = 0;

        for (uint32_t row = 0; row < image.Height; ++row)
        {
            for (uint32_t x = 0; x < image.Width; ++x)
            {
                lowest = std::min(lowest, image.At(x, row).r);
                highest = std::max(highest, image.At(x, row).r);
            }
        }

        return highest - lowest;
    };

    nlohmann::json settings = assets.GetInfo(texture)->Settings;

    settings["generateMips"] = false;
    REQUIRE(assets.ReimportTexture(texture, settings));
    const int withoutMips = spread();

    settings["generateMips"] = true;
    REQUIRE(assets.ReimportTexture(texture, settings));
    const int withMips = spread();

    CAPTURE(withoutMips, withMips);
    CHECK(withMips < 8);
    CHECK(withoutMips > withMips);
}

TEST_CASE_METHOD(Fixture, "Flip Y turns the picture upside down", "[asset editing]")
{
    Project.Write("rows.bmp", MakeBmp(1, 2, { Rgb{ 255, 0, 0 }, Rgb{ 0, 0, 255 } }));
    Project.Write("rows.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "cull": "none", "properties": { "Color": [1, 1, 1, 1] }, "textures": { "ColorMap": "rows.bmp" } })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const TextureID texture = assets.Find<AssetType::Texture>("rows.bmp");

    nlohmann::json settings = assets.GetInfo(texture)->Settings;
    settings["generateMips"] = false;

    settings["flipY"] = false;
    REQUIRE(assets.ReimportTexture(texture, settings));
    const Image normal = Draw(assets, Target, "rows.mat");

    settings["flipY"] = true;
    REQUIRE(assets.ReimportTexture(texture, settings));
    const Image flipped = Draw(assets, Target, "rows.mat");

    const glm::ivec4 topBefore = normal.AtClip(0.0f, 0.9f);
    const glm::ivec4 bottomBefore = normal.AtClip(0.0f, -0.9f);

    CAPTURE(topBefore.r, topBefore.b, bottomBefore.r, bottomBefore.b);
    CHECK(topBefore != bottomBefore);
    CHECK(IsNear(flipped.AtClip(0.0f, 0.9f), glm::ivec3(bottomBefore), 2));
    CHECK(IsNear(flipped.AtClip(0.0f, -0.9f), glm::ivec3(topBefore), 2));
}

TEST_CASE_METHOD(Fixture, "An sRGB texture is decoded to linear when sampled", "[asset editing]")
{
    // 188 is the sRGB encoding of 0.5
    Project.Write("gray.bmp", MakeBmp(1, 1, { Rgb{ 188, 188, 188 } }));
    Project.Write("unlit.hlsl", kUnlitShader);
    Project.Write("unlit.mat", R"({ "shader": "unlit.hlsl", "cull": "none", "textures": { "Picture": "gray.bmp" } })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());

    CHECK(IsNear(Draw(assets, Target, "unlit.mat").AtClip(0.0f, 0.0f), glm::ivec3(127, 127, 127), 2));

    SECTION("Not when it is turned off")
    {
        REQUIRE(assets.ReimportTexture(assets.Find<AssetType::Texture>("gray.bmp"), { { "srgb", false } }));
        CHECK(IsNear(Draw(assets, Target, "unlit.mat").AtClip(0.0f, 0.0f), glm::ivec3(188, 188, 188), 2));
    }

    Gpu().Device->WaitIdle();
}
