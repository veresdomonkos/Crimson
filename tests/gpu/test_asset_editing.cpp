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

    struct Rgb
    {
        unsigned char R = 0, G = 0, B = 0;
    };

    void AppendLittleEndian(std::string& out, uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }

    // An uncompressed BMP. The pixels go from the top row down, the file stores them the other way around.
    std::string MakeBmp(uint32_t width, uint32_t height, const std::vector<Rgb>& pixels)
    {
        const uint32_t rowSize = (width * 3 + 3) / 4 * 4;

        std::string out = "BM";
        AppendLittleEndian(out, 54 + rowSize * height, 4);
        AppendLittleEndian(out, 0, 4);
        AppendLittleEndian(out, 54, 4);
        AppendLittleEndian(out, 40, 4);
        AppendLittleEndian(out, width, 4);
        AppendLittleEndian(out, height, 4);
        AppendLittleEndian(out, 1, 2);
        AppendLittleEndian(out, 24, 2);
        AppendLittleEndian(out, 0, 4);
        AppendLittleEndian(out, rowSize * height, 4);
        AppendLittleEndian(out, 0, 16);

        for (uint32_t row = height; row-- > 0;)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const Rgb& pixel = pixels[row * width + x];
                out.push_back(static_cast<char>(pixel.B));
                out.push_back(static_cast<char>(pixel.G));
                out.push_back(static_cast<char>(pixel.R));
            }

            out.append(rowSize - width * 3, '\0');
        }

        return out;
    }

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
        FrameLightingData Lighting;

        Fixture()
        {
            Lighting.AmbientColor = glm::vec3(1.0f);

            Project.Write("quad.obj", kFullQuad);
            Project.Write("mine.mat", R"({ "shader": "engine/shaders/main.hlsl", "cull": "none", "properties": { "Color": [1, 0, 0, 1] } })");
        }

        Image Draw(AssetManager& assets, const OffscreenTarget& target, const char* material = "mine.mat")
        {
            const Mesh* mesh = assets.LoadMesh("quad.obj");
            const MaterialHandle handle = assets.LoadMaterial(material);
            REQUIRE(mesh);
            REQUIRE(handle);

            const RenderPassInfo pass{ .Target = target.Handle(), .ClearFlags = ClearFlags::Color | ClearFlags::Depth };

            RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(pass).Draw(*mesh, 0, handle); }, Lighting);

            return target.Read();
        }
    };
}

TEST_CASE_METHOD(Fixture, "Editing a material shows on the next frame", "[asset editing]")
{
    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const AssetID id = assets.FindId("mine.mat");

    REQUIRE(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(255, 0, 0)));

    MaterialData data = *assets.GetMaterialData(id);

    SECTION("A property")
    {
        data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };

        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        CHECK(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(0, 255, 0)));
    }

    SECTION("The settings: a mesh facing away disappears when back faces are culled")
    {
        Project.Write("away.obj", "v -1 1 0.5\nv 1 1 0.5\nv 1 -1 0.5\nv -1 -1 0.5\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n");
        assets.Refresh();

        const auto drawAway = [&]
        {
            const Mesh* mesh = assets.LoadMesh("away.obj");
            REQUIRE(mesh);

            const MaterialHandle handle = assets.LoadMaterial(id);
            const RenderPassInfo pass{ .Target = Target.Handle(), .ClearFlags = ClearFlags::Color | ClearFlags::Depth };

            RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(pass).Draw(*mesh, 0, handle); }, Lighting);

            return Target.Read().AtClip(0.0f, 0.0f);
        };

        const glm::ivec4 noCulling = drawAway();

        data.Settings.Cull = CullMode::Back;
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        const glm::ivec4 backCulling = drawAway();

        data.Settings.Cull = CullMode::Front;
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        const glm::ivec4 frontCulling = drawAway();

        CHECK(IsNear(noCulling, glm::ivec3(255, 0, 0)));

        // Which of the two hides it depends on the winding, but it can only be one of them
        CHECK(IsNear(backCulling, glm::ivec3(0, 0, 0)) != IsNear(frontCulling, glm::ivec3(0, 0, 0)));
    }

    SECTION("Another shader, which makes a new material")
    {
        Project.Write("green.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return mul(ViewProj, mul(GetObjectData().Transform, float4(position, 1.0))); }
float4 PSMain() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }
)");
        assets.Refresh();

        data.Shader = assets.FindId("green.hlsl");
        data.Properties.clear();

        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Replaced);
        CHECK(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(0, 255, 0)));
    }

    SECTION("Reverting to the file brings it back")
    {
        data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        REQUIRE(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(0, 255, 0)));

        const auto saved = assets.ReadMaterialFile(id);
        REQUIRE(saved);
        REQUIRE(assets.ApplyMaterial(id, *saved) == MaterialApplyResult::Updated);
        CHECK(IsNear(Draw(assets, Target).AtClip(0.0f, 0.0f), glm::ivec3(255, 0, 0)));
    }
}

TEST_CASE_METHOD(Fixture, "A material's color map multiplies its color", "[asset editing]")
{
    Project.Write("blue.bmp", MakeBmp(1, 1, { Rgb{ 0, 0, 255 } }));
    Project.Write("white.mat", R"({ "shader": "engine/shaders/main.hlsl", "cull": "none", "properties": { "Color": [1, 1, 1, 1] } })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const AssetID id = assets.FindId("white.mat");

    REQUIRE(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 255)));

    MaterialData data = *assets.GetMaterialData(id);
    data.Textures["ColorMap"] = assets.FindId("blue.bmp");
    REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);

    CHECK(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(0, 0, 255)));

    SECTION("Clearing it goes back to white")
    {
        data.Textures.clear();
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);

        CHECK(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 255)));
    }

    SECTION("Deleting the texture goes back to white too")
    {
        REQUIRE(assets.Delete(assets.FindId("blue.bmp")));

        CHECK(IsNear(Draw(assets, Target, "white.mat").AtClip(0.0f, 0.0f), glm::ivec3(255, 255, 255)));
    }
}

TEST_CASE("Mipmaps keep a shrunken texture from flickering", "[asset editing]")
{
    TempFolder project;
    project.Write("quad.obj", kFullQuad);
    project.Write("checker.bmp", MakeCheckerboard(256));
    project.Write("mine.mat", R"({ "shader": "engine/shaders/main.hlsl", "cull": "none", "properties": { "Color": [1, 1, 1, 1] }, "textures": { "ColorMap": "checker.bmp" } })");

    // A size that doesn't divide the texture evenly, so without mips the samples land on different mixes of the two colors
    const OffscreenTarget target(37, 37);
    const SceneRenderer shadows = MakeSceneRenderer();

    AssetManager assets(*Gpu().GPUResources, project.Path());
    const AssetID texture = assets.FindId("checker.bmp");
    FrameLightingData lighting;
    lighting.AmbientColor = glm::vec3(1.0f);

    const auto spread = [&]
    {
        const Mesh* mesh = assets.LoadMesh("quad.obj");
        const MaterialHandle material = assets.LoadMaterial("mine.mat");
        REQUIRE(mesh);
        REQUIRE(material);

        const RenderPassInfo pass{ .Target = target.Handle(), .ClearFlags = ClearFlags::Color | ClearFlags::Depth };
        RenderOneFrame([&](FrameContext& frame) { frame.BeginRenderPass(pass).Draw(*mesh, 0, material); }, lighting);

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
    Project.Write("rows.mat", R"({ "shader": "engine/shaders/main.hlsl", "cull": "none", "properties": { "Color": [1, 1, 1, 1] }, "textures": { "ColorMap": "rows.bmp" } })");

    AssetManager assets(*Gpu().GPUResources, Project.Path());
    const AssetID texture = assets.FindId("rows.bmp");

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
