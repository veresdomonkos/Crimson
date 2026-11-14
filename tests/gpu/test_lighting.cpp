#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <glm/gtc/quaternion.hpp>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "gpu/gpu_test.hpp"
#include "support/scene_fields.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;

namespace
{
    constexpr uint32_t kSize = 128;

    // Two lit points on a flat surface can differ by this much, as they see the light's reflection from different angles
    constexpr float kSpecularDifference = 0.02f;

    constexpr glm::vec4 kClearColor(0.0f, 0.0f, 0.2f, 1.0f);
    constexpr glm::ivec3 kClearPixel(0, 0, 124); // the clear color is linear, this is its sRGB encoding

    // Facing +Z when seen with x going right and y going up
    constexpr std::string_view kWall = "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";

    // Facing +Y, from above
    constexpr std::string_view kFloor = "v -1 0 1\nv 1 0 1\nv 1 0 -1\nv -1 0 -1\nvn 0 1 0\nf 1//1 2//1 3//1 4//1\n";

    int Brightness(const glm::ivec4& color)
    {
        return color.r + color.g + color.b;
    }

    struct Fixture
    {
        TempFolder Project;
        OffscreenTarget Target{ kSize, kSize };
        PerspectiveCamera Camera{ glm::vec3(0.0f, 0.0f, 3.0f), 70.0f, 1.0f };
        Scene World;
        FrameLighting Lighting;

        std::unique_ptr<AssetManager> Assets;
        std::unique_ptr<SceneRenderer> Subject;

        Fixture()
        {
            Project.Write("wall.obj", kWall);
            Project.Write("floor.obj", kFloor);
            Project.Write("white.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "Color": [1, 1, 1, 1] } })");

            Assets = std::make_unique<AssetManager>(*Gpu().GPUResources, Project.Path());
            Subject = std::make_unique<SceneRenderer>(*Gpu().GPUResources, *Gpu().Renderer, ShadowAtlasSettings{ .AtlasSize = 1024, .TileSize = 256 });

            Subject->SetToneMapMaterial(Assets->Load<AssetType::Material>("engine/materials/internal/M_ToneMap.mat"), kSize, kSize);
            Subject->SetToneMapping(1.0f, ToneMapper::None); // so that expected colors only need the sRGB encoding
            Subject->SetOutlineMaterial(Assets->Load<AssetType::Material>("engine/materials/internal/M_Outline.mat"), kSize, kSize);

            Lighting.AmbientColor = glm::vec3(0.0f);
        }

        ~Fixture()
        {
            Gpu().Device->WaitIdle();
        }

        Entity AddMesh(const char* mesh, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f))
        {
            const Entity entity = World.CreateEntity(mesh);

            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Position = position;
            transform.Scale = scale;

            auto& renderer = entity.AddComponent<MeshRendererComponent>();
            renderer.MeshAsset = Assets->Find<AssetType::Mesh>(mesh);
            renderer.MaterialAssets = { Assets->Find<AssetType::Material>("white.mat") };
            ResolveAssets(*Assets, renderer);
            REQUIRE_FALSE(renderer.Resolved.SubMeshes.empty());

            return entity;
        }

        Entity AddLight(const LightComponent& light, const glm::vec3& position, const glm::vec3& direction)
        {
            const Entity entity = World.CreateEntity("Light");
            entity.AddComponent<LightComponent>(light);

            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Position = position;
            transform.RotationDegrees = glm::degrees(glm::eulerAngles(glm::quatLookAt(glm::normalize(direction), glm::vec3(0.0f, 1.0f, 0.1f))));

            return entity;
        }

        Image Render(Entity outlined = {})
        {
            World.UpdateWorldTransforms();
            Subject->BuildLighting(World, Lighting);

            const PassInfo main{
                .Target = Target.Handle(),
                .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = kClearColor },
                .View = { .ViewProj = Camera.GetViewProj(), .CameraPosition = Camera.GetPosition() }
            };

            RenderOneFrame([&](FrameRecorder& frame) { Subject->Record(frame, World, main, outlined); }, Lighting);

            return Target.Read();
        }

        // Where a point in the world lands in the image
        [[nodiscard]] glm::ivec4 PixelAt(const Image& image, const glm::vec3& world) const
        {
            const glm::vec4 clip = Camera.GetViewProj() * glm::vec4(world, 1.0f);
            return image.AtClip(clip.x / clip.w, clip.y / clip.w);
        }
    };
}

TEST_CASE_METHOD(Fixture, "The camera sees a mesh's front but not its back", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(1.0f);

    const Entity wall = AddMesh("wall.obj");

    SECTION("From the front")
    {
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(255, 255, 255)));
    }

    SECTION("From behind")
    {
        wall.GetComponent<TransformComponent>().RotationDegrees = glm::vec3(0.0f, 180.0f, 0.0f);
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), kClearPixel));
    }
}

TEST_CASE_METHOD(Fixture, "A surface is lit by what faces it", "[lighting]")
{
    AddMesh("wall.obj");

    SECTION("Without lights it is black")
    {
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(0, 0, 0)));
    }

    SECTION("Ambient light is added evenly")
    {
        Lighting.AmbientColor = glm::vec3(0.5f);
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(188, 188, 188), 4));
    }

    SECTION("A directional light from the front lights it")
    {
        AddLight({ .Type = LightType::Directional, .Color = glm::vec3(1.0f, 0.0f, 0.0f) }, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

        const glm::ivec4 pixel = PixelAt(Render(), glm::vec3(0.0f));
        CAPTURE(pixel.r, pixel.g, pixel.b);
        CHECK(pixel.r > 100);
        CHECK(pixel.g == 0);
    }

    SECTION("A directional light from behind doesn't")
    {
        AddLight({ .Type = LightType::Directional }, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(0, 0, 0)));
    }

    SECTION("A point light fades with distance")
    {
        AddLight({ .Type = LightType::Point, .Intensity = 2.0f, .Range = 2.5f }, glm::vec3(0.0f, -0.8f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f));

        const Image image = Render();
        CHECK(Brightness(PixelAt(image, glm::vec3(0.0f, -0.8f, 0.0f))) > Brightness(PixelAt(image, glm::vec3(0.0f, 0.8f, 0.0f))));
    }

    SECTION("A spot light stays inside its cone")
    {
        AddLight({ .Type = LightType::Spot, .Intensity = 3.0f, .Range = 10.0f, .InnerConeDegrees = 10.0f, .OuterConeDegrees = 15.0f },
            glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f));

        const Image image = Render();
        CHECK(Brightness(PixelAt(image, glm::vec3(0.0f))) > 100);
        CHECK(IsNear(PixelAt(image, glm::vec3(0.9f, 0.9f, 0.0f)), glm::ivec3(0, 0, 0)));
    }

    SECTION("An inactive light does nothing")
    {
        const Entity light = AddLight({ .Type = LightType::Directional }, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        light.GetComponent<NameComponent>().Active = false;
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(0, 0, 0)));
    }
}

TEST_CASE_METHOD(Fixture, "The scene is tone mapped from linear HDR values", "[lighting]")
{
    AddMesh("wall.obj");

    SECTION("Without a tone mapper, only the sRGB encoding is left")
    {
        Lighting.AmbientColor = glm::vec3(0.5f);
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(188, 188, 188), 4));
    }

    SECTION("Without a tone mapper, what is too bright clamps")
    {
        Lighting.AmbientColor = glm::vec3(4.0f);
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(255, 255, 255)));
    }

    SECTION("Light above 1 survives until exposure brings it back")
    {
        Lighting.AmbientColor = glm::vec3(5.0f);
        Subject->SetToneMapping(0.1f, ToneMapper::None);
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(188, 188, 188), 4));
    }

    SECTION("ACES follows its curve")
    {
        // 1 comes out as 0.8038, which encodes to 231
        Lighting.AmbientColor = glm::vec3(1.0f);
        Subject->SetToneMapping(1.0f, ToneMapper::Aces);
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(231, 231, 231), 4));
    }

    SECTION("ACES tells very bright values apart, where clamping can't")
    {
        Subject->SetToneMapping(1.0f, ToneMapper::Aces);

        Lighting.AmbientColor = glm::vec3(1.5f);
        const int bright = Brightness(PixelAt(Render(), glm::vec3(0.0f)));

        Lighting.AmbientColor = glm::vec3(3.0f);
        const int brighter = Brightness(PixelAt(Render(), glm::vec3(0.0f)));

        CAPTURE(bright, brighter);
        CHECK(brighter > bright);
        CHECK(brighter < 255 * 3);
    }
}

// Light comes from the upper left, so the small roof's shadow falls to its right on the floor. The camera looks straight down.
struct ShadowFixture : Fixture
{
    static constexpr glm::vec3 kInShadow{ 1.0f, 0.0f, 0.0f };
    static constexpr glm::vec3 kInLight{ -3.0f, 0.0f, 0.0f };

    Entity Roof;

    ShadowFixture()
    {
        Camera.SetPosition(glm::vec3(0.0f, 8.0f, 0.0f));
        Camera.SetRotation(-90.0f, -89.9f);

        AddMesh("floor.obj", glm::vec3(0.0f), glm::vec3(6.0f, 1.0f, 6.0f));
        Roof = AddMesh("floor.obj", glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.5f, 1.0f, 0.5f));
    }
};

TEST_CASE_METHOD(ShadowFixture, "A mesh casts a shadow from a directional light", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);

    AddLight({ .Type = LightType::Directional, .CastsShadow = true }, glm::vec3(0.0f), glm::vec3(1.0f, -1.0f, 0.0f));

    SECTION("Where it falls is darker")
    {
        const Image image = Render();
        const float shadow = LinearBrightness(PixelAt(image, kInShadow));
        const float light = LinearBrightness(PixelAt(image, kInLight));

        CAPTURE(shadow, light);
        CHECK(shadow < light / 2);
    }

    SECTION("A mesh that doesn't cast shadows leaves none")
    {
        Roof.GetComponent<MeshRendererComponent>().CastsShadows = false;

        const Image image = Render();
        const float shadow = LinearBrightness(PixelAt(image, kInShadow));
        const float light = LinearBrightness(PixelAt(image, kInLight));

        CAPTURE(shadow, light);
        CHECK(std::abs(shadow - light) <= kSpecularDifference);
    }
}

TEST_CASE_METHOD(ShadowFixture, "A light that doesn't cast shadows goes through meshes", "[lighting]")
{
    AddLight({ .Type = LightType::Directional, .CastsShadow = false }, glm::vec3(0.0f), glm::vec3(1.0f, -1.0f, 0.0f));

    const Image image = Render();
    CHECK(std::abs(LinearBrightness(PixelAt(image, kInShadow)) - LinearBrightness(PixelAt(image, kInLight))) <= kSpecularDifference);
}

TEST_CASE_METHOD(ShadowFixture, "A spot light casts a shadow", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);

    AddLight({ .Type = LightType::Spot, .Intensity = 8.0f, .Range = 20.0f, .InnerConeDegrees = 60.0f, .OuterConeDegrees = 70.0f, .CastsShadow = true },
        glm::vec3(-1.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    const Image image = Render();
    const float shadow = LinearBrightness(PixelAt(image, kInShadow));
    const float light = LinearBrightness(PixelAt(image, glm::vec3(-3.0f, 0.0f, 0.0f)));

    CAPTURE(shadow, light);
    CHECK(shadow < light);
}

TEST_CASE_METHOD(ShadowFixture, "A point light casts a shadow", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);

    AddLight({ .Type = LightType::Point, .Intensity = 12.0f, .Range = 20.0f, .CastsShadow = true },
        glm::vec3(-1.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    const Image image = Render();
    const float light = LinearBrightness(PixelAt(image, glm::vec3(-3.0f, 0.0f, 0.0f)));

    // The point light has six views, so these two fall on different ones: straight down, and to the side
    for (const float x : { 0.9f, 1.1f })
    {
        const float shadow = LinearBrightness(PixelAt(image, glm::vec3(x, 0.0f, 0.0f)));

        CAPTURE(x, shadow, light);
        CHECK(shadow < light / 2);
    }

    const float seam = LinearBrightness(PixelAt(image, kInShadow));
    INFO("seam brightness " << seam << " light " << light);
    CHECK(seam < light / 2);
}

TEST_CASE_METHOD(ShadowFixture, "The last of 64 shadowed point lights still casts its shadow", "[lighting]")
{
    // 1024 tiles, for 384 views. Far more than a frame has passes, they are all drawn by one.
    Gpu().Device->WaitIdle();
    Subject.reset();
    Subject = std::make_unique<SceneRenderer>(*Gpu().GPUResources, *Gpu().Renderer, ShadowAtlasSettings{ .AtlasSize = 4096, .TileSize = 128 });
    Subject->SetToneMapMaterial(Assets->Load<AssetType::Material>("engine/materials/internal/M_ToneMap.mat"), kSize, kSize);

    Lighting.AmbientColor = glm::vec3(0.1f);

    // These take the first 378 tiles and give no light
    for (int i = 0; i < 63; ++i)
        AddLight({ .Type = LightType::Point, .Intensity = 0.0f, .Range = 20.0f, .CastsShadow = true }, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    AddLight({ .Type = LightType::Point, .Intensity = 12.0f, .Range = 20.0f, .CastsShadow = true },
        glm::vec3(-1.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    const Image image = Render();

    REQUIRE(Lighting.Lights.size() == 64);
    for (const Light& light : Lighting.Lights)
        CHECK(light.ShadowIndex >= 0);

    const float light = LinearBrightness(PixelAt(image, kInLight));

    // On two of the last light's six views, as in the test above
    for (const float x : { 0.9f, 1.1f })
    {
        const float shadow = LinearBrightness(PixelAt(image, glm::vec3(x, 0.0f, 0.0f)));

        CAPTURE(x, shadow, light);
        CHECK(shadow < light / 2);
    }
}

namespace
{
    // With UVs: U goes along +X and V along +Y
    constexpr std::string_view kTexturedWall =
        "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n";

    struct MaterialFixture : Fixture
    {
        int MaterialCount = 0;

        Entity AddWall(const std::string& properties, const std::string& textures = "{}",
            const std::string& shader = "engine/shaders/S_Lit.hlsl", const char* mesh = "textured_wall.obj")
        {
            Project.Write("textured_wall.obj", kTexturedWall);

            const std::string name = "wall" + std::to_string(MaterialCount++) + ".mat";
            Project.Write(name, R"({ "shader": ")" + shader + R"(", "properties": )" + properties + R"(, "textures": )" + textures + " }");
            Assets->Refresh();

            const Entity entity = World.CreateEntity(name);
            auto& renderer = entity.AddComponent<MeshRendererComponent>();
            renderer.MeshAsset = Assets->Find<AssetType::Mesh>(mesh);
            renderer.MaterialAssets = { Assets->Find<AssetType::Material>(name) };
            ResolveAssets(*Assets, renderer);
            REQUIRE_FALSE(renderer.Resolved.SubMeshes.empty());

            return entity;
        }

        // Data textures hold values and not colors
        void WriteDataTexture(const std::string& name, Rgb color)
        {
            Project.Write(name, MakeBmp(1, 1, { color }));
            Assets->Refresh();
            REQUIRE(Assets->Reimport(Assets->Find<AssetType::Texture>(name), { { "srgb", false } }));
        }

        float RenderCenter()
        {
            return LinearBrightness(PixelAt(Render(), glm::vec3(0.0f)));
        }

        // A wall drawn alone: its center and a point off to the side of it
        std::array<glm::ivec4, 2> RenderWall(const std::string& properties, const std::string& textures, const std::string& shader,
            const char* mesh = "textured_wall.obj")
        {
            const Entity wall = AddWall(properties, textures, shader, mesh);
            const Image image = Render();
            World.DestroyEntity(wall);

            return { PixelAt(image, glm::vec3(0.0f)), PixelAt(image, glm::vec3(0.5f, 0.5f, 0.0f)) };
        }
    };
}

TEST_CASE_METHOD(MaterialFixture, "A metal has no diffuse light", "[pbr]")
{
    // From the side, so its reflection goes away from the camera
    SECTION("A dielectric scatters it towards the camera")
    {
        AddLight({ .Type = LightType::Directional, .Intensity = 3.0f }, glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, -1.0f));
        AddWall(R"({ "Metallic": 0, "Roughness": 0.2 })");
        CHECK(RenderCenter() > 0.3f);
    }

    SECTION("A metal doesn't")
    {
        AddLight({ .Type = LightType::Directional, .Intensity = 3.0f }, glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, -1.0f));
        AddWall(R"({ "Metallic": 1, "Roughness": 0.2 })");
        CHECK(RenderCenter() < 0.1f);
    }

    SECTION("But it reflects ambient light in its own color")
    {
        Lighting.AmbientColor = glm::vec3(0.5f);

        AddWall(R"({ "Color": [1, 0, 0, 1], "Metallic": 1, "Roughness": 0.5 })");
        const glm::ivec4 pixel = PixelAt(Render(), glm::vec3(0.0f));

        CAPTURE(pixel.r, pixel.g, pixel.b);
        CHECK(pixel.r > 100);
        CHECK(pixel.g < pixel.r / 3);
    }
}

TEST_CASE_METHOD(MaterialFixture, "A smoother surface has a smaller and brighter highlight", "[pbr]")
{
    AddLight({ .Type = LightType::Directional }, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    const glm::vec3 kOffCenter(0.9f, 0.0f, 0.0f);

    const Entity smooth = AddWall(R"({ "Metallic": 1, "Roughness": 0.2 })");
    const Image smoothImage = Render();

    World.DestroyEntity(smooth);
    AddWall(R"({ "Metallic": 1, "Roughness": 0.6 })");
    const Image roughImage = Render();

    const float smoothCenter = LinearBrightness(PixelAt(smoothImage, glm::vec3(0.0f)));
    const float roughCenter = LinearBrightness(PixelAt(roughImage, glm::vec3(0.0f)));
    const float smoothEdge = LinearBrightness(PixelAt(smoothImage, kOffCenter));
    const float roughEdge = LinearBrightness(PixelAt(roughImage, kOffCenter));

    CAPTURE(smoothCenter, roughCenter, smoothEdge, roughEdge);
    CHECK(smoothCenter > roughCenter);
    CHECK(smoothEdge < roughEdge);
}

TEST_CASE_METHOD(MaterialFixture, "Light falls off with the square of the distance", "[pbr]")
{
    AddWall(R"({ "Roughness": 1 })");

    const Entity light = AddLight({ .Type = LightType::Point, .Intensity = 2.0f, .Range = 1000.0f }, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    const float nearLight = RenderCenter();

    light.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, 0.0f, 2.0f);
    const float farLight = RenderCenter();

    CAPTURE(nearLight, farLight);
    CHECK(nearLight / farLight > 3.5f);
    CHECK(nearLight / farLight < 4.5f);
}

TEST_CASE_METHOD(MaterialFixture, "A point light stops at its range", "[pbr]")
{
    AddWall("{}");
    AddLight({ .Type = LightType::Point, .Intensity = 100.0f, .Range = 1.0f }, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(0, 0, 0)));
}

TEST_CASE_METHOD(MaterialFixture, "A normal map bends the light the same way on every backend", "[pbr]")
{
    const glm::vec3 kFromRight(-1.0f, 0.0f, -1.0f);
    const glm::vec3 kFromLeft(1.0f, 0.0f, -1.0f);

    const auto render = [&](const glm::vec3& direction)
    {
        const Entity light = AddLight({ .Type = LightType::Directional, .Intensity = 2.0f }, glm::vec3(0.0f), direction);
        const float result = RenderCenter();
        World.DestroyEntity(light);
        return result;
    };

    SECTION("Without one, both sides are the same")
    {
        AddWall(R"({ "Roughness": 1 })");
        CHECK(std::abs(render(kFromRight) - render(kFromLeft)) <= kSpecularDifference);
    }

    SECTION("Leaning towards +U, it faces a light on that side")
    {
        WriteDataTexture("lean.bmp", Rgb{ 218, 128, 218 });
        AddWall(R"({ "Roughness": 1 })", R"({ "NormalMap": "lean.bmp" })");

        const float right = render(kFromRight);
        const float left = render(kFromLeft);

        CAPTURE(right, left);
        CHECK(right > left * 3.0f);
    }

    SECTION("Leaning up in the image, it faces a light above")
    {
        // The mesh's V is flipped on import, so the top of the image is at the top of the wall
        WriteDataTexture("lean_up.bmp", Rgb{ 128, 218, 218 });
        AddWall(R"({ "Roughness": 1 })", R"({ "NormalMap": "lean_up.bmp" })");

        const float above = render(glm::vec3(0.0f, -1.0f, -1.0f));
        const float below = render(glm::vec3(0.0f, 1.0f, -1.0f));

        CAPTURE(above, below);
        CHECK(above > below * 3.0f);
    }

    SECTION("Without UVs there is nothing to go by, and it stays flat")
    {
        WriteDataTexture("lean.bmp", Rgb{ 218, 128, 218 });
        Project.Write("nouv.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "Roughness": 1 }, "textures": { "NormalMap": "lean.bmp" } })");
        Assets->Refresh();

        auto& renderer = AddMesh("wall.obj").GetComponent<MeshRendererComponent>();
        renderer.MaterialAssets = { Assets->Find<AssetType::Material>("nouv.mat") };
        ResolveAssets(*Assets, renderer);

        const float right = render(kFromRight);
        const float left = render(kFromLeft);

        CAPTURE(right, left);
        CHECK(right > 0.1f);
        CHECK(std::abs(right - left) <= kSpecularDifference);
    }
}

TEST_CASE_METHOD(MaterialFixture, "Maps feed the material's values", "[pbr]")
{
    SECTION("Emissive shows with no light at all")
    {
        AddWall(R"({ "EmissiveColor": [1, 0, 0], "EmissiveStrength": 0.5 })");
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(188, 0, 0), 4));
    }

    SECTION("Emissive can go above 1")
    {
        Subject->SetToneMapping(0.25f, ToneMapper::None);
        AddWall(R"({ "EmissiveColor": [1, 0, 0], "EmissiveStrength": 2 })");
        CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), glm::ivec3(188, 0, 0), 4));
    }

    SECTION("Occlusion darkens ambient light, as far as its strength says")
    {
        Lighting.AmbientColor = glm::vec3(0.5f);
        WriteDataTexture("dark.bmp", Rgb{ 0, 255, 255 });

        const Entity full = AddWall(R"({ "OcclusionStrength": 1 })", R"({ "OcclusionMap": "dark.bmp" })");
        CHECK(RenderCenter() < 0.01f);

        World.DestroyEntity(full);
        AddWall(R"({ "OcclusionStrength": 0 })", R"({ "OcclusionMap": "dark.bmp" })");
        CHECK(RenderCenter() > 0.45f);
    }

    SECTION("Blue limits how metallic it is, as a metal has no diffuse light")
    {
        AddLight({ .Type = LightType::Directional, .Intensity = 3.0f }, glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, -1.0f));
        WriteDataTexture("nonmetal.bmp", Rgb{ 255, 255, 0 });

        AddWall(R"({ "Metallic": 1, "Roughness": 0.2 })", R"({ "MetallicRoughnessMap": "nonmetal.bmp" })");
        CHECK(RenderCenter() > 0.3f);
    }

    SECTION("Green limits how rough it is, so a dark one makes the highlight sharper")
    {
        AddLight({ .Type = LightType::Directional }, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        WriteDataTexture("smooth.bmp", Rgb{ 255, 64, 255 });

        const Entity rough = AddWall(R"({ "Metallic": 1, "Roughness": 0.8 })");
        const float roughCenter = RenderCenter();

        World.DestroyEntity(rough);
        AddWall(R"({ "Metallic": 1, "Roughness": 0.8 })", R"({ "MetallicRoughnessMap": "smooth.bmp" })");
        const float smoothCenter = RenderCenter();

        CAPTURE(roughCenter, smoothCenter);
        CHECK(smoothCenter > roughCenter * 2.0f);
    }
}

TEST_CASE_METHOD(Fixture, "A selected mesh gets an outline", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.3f);

    const Entity wall = AddMesh("wall.obj", glm::vec3(0.0f), glm::vec3(0.5f));

    const auto countOutlinePixels = [](const Image& image)
    {
        int count = 0;

        for (uint32_t row = 0; row < image.Height; ++row)
            for (uint32_t x = 0; x < image.Width; ++x)
                if (IsNear(image.At(x, row), glm::ivec3(255, 166, 26), 40))
                    ++count;

        return count;
    };

    CHECK(countOutlinePixels(Render()) == 0);

    SECTION("Selected")
    {
        const Image image = Render(wall);
        CHECK(countOutlinePixels(image) > 20);

        // Around it, and not over it
        CHECK_FALSE(IsNear(PixelAt(image, glm::vec3(0.0f)), glm::ivec3(255, 166, 26), 40));

        // The same color as with no tone mapping, as it is drawn after that
        Subject->SetToneMapping(0.5f, ToneMapper::Aces);
        const Image mapped = Render(wall);
        int exact = 0;

        for (uint32_t row = 0; row < mapped.Height; ++row)
            for (uint32_t x = 0; x < mapped.Width; ++x)
                if (IsNear(mapped.At(x, row), glm::ivec3(255, 166, 26), 2))
                    ++exact;

        CHECK(exact > 20);
    }

    SECTION("Goes away when the selection does")
    {
        CHECK(countOutlinePixels(Render(wall)) > 20);
        CHECK(countOutlinePixels(Render()) == 0);
    }
}

namespace
{
    // Every shader that has a cutout, and the ones that light a surface
    constexpr std::array kCutoutShaders = {
        "S_Lit", "S_LitMask", "S_LitMaskParallax", "S_LitMaskTriplanar", "S_LitMaskBiplanar", "S_Unlit"
    };
    constexpr size_t kLitShaders = 5;

    // Cutout materials whose alpha is all under the cutoff, and all over it
    std::string CutAway(std::string_view shader)
    {
        return fmt::format(R"({{ "shader": "engine/shaders/{}.hlsl", "renderMode": "cutout", "properties": {{ "Color": [1, 1, 1, 0] }} }})", shader);
    }

    std::string CutNothing(std::string_view shader)
    {
        return fmt::format(R"({{ "shader": "engine/shaders/{}.hlsl", "renderMode": "cutout", "properties": {{ "Color": [1, 1, 1, 1] }} }})", shader);
    }

    void UseMaterial(Entity entity, AssetManager& assets, std::string_view path)
    {
        auto& renderer = entity.GetComponent<MeshRendererComponent>();
        renderer.MaterialAssets = { assets.Find<AssetType::Material>(path) };
        ResolveAssets(assets, renderer);
    }
}

TEST_CASE_METHOD(Fixture, "What a cutout material cuts away isn't drawn", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.3f);

    for (const std::string_view shader : kCutoutShaders)
    {
        DYNAMIC_SECTION(shader)
        {
            Project.Write("cut_away.mat", CutAway(shader));
            Project.Write("cut_nothing.mat", CutNothing(shader));
            Assets->Refresh();

            const Entity wall = AddMesh("wall.obj", glm::vec3(0.0f), glm::vec3(0.5f));

            UseMaterial(wall, *Assets, "cut_away.mat");
            CHECK(IsNear(PixelAt(Render(), glm::vec3(0.0f)), kClearPixel, 2));

            UseMaterial(wall, *Assets, "cut_nothing.mat");
            CHECK_FALSE(IsNear(PixelAt(Render(), glm::vec3(0.0f)), kClearPixel, 2));
        }
    }
}

// Each shader has a PSDepth of its own, so each one is checked
TEST_CASE_METHOD(ShadowFixture, "What a cutout material cuts away casts no shadow", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);

    AddLight({ .Type = LightType::Directional, .CastsShadow = true }, glm::vec3(0.0f), glm::vec3(1.0f, -1.0f, 0.0f));

    for (const std::string_view shader : std::span(kCutoutShaders).first(kLitShaders))
    {
        DYNAMIC_SECTION(shader)
        {
            Project.Write("cut_away.mat", CutAway(shader));
            Project.Write("cut_nothing.mat", CutNothing(shader));
            Assets->Refresh();

            SECTION("All of it cut away, so no shadow")
            {
                UseMaterial(Roof, *Assets, "cut_away.mat");

                const Image image = Render();
                const float shadow = LinearBrightness(PixelAt(image, kInShadow));
                const float light = LinearBrightness(PixelAt(image, kInLight));

                CAPTURE(shadow, light);
                CHECK(std::abs(shadow - light) <= kSpecularDifference);
            }

            SECTION("Nothing cut away, so the whole shadow")
            {
                UseMaterial(Roof, *Assets, "cut_nothing.mat");

                const Image image = Render();
                const float shadow = LinearBrightness(PixelAt(image, kInShadow));
                const float light = LinearBrightness(PixelAt(image, kInLight));

                CAPTURE(shadow, light);
                CHECK(shadow < light / 2);
            }
        }
    }
}

TEST_CASE_METHOD(Fixture, "What a cutout material cuts away has no outline", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.3f);
    Project.Write("cut_away.mat", CutAway("S_Lit"));
    Assets->Refresh();

    const Entity wall = AddMesh("wall.obj", glm::vec3(0.0f), glm::vec3(0.5f));
    UseMaterial(wall, *Assets, "cut_away.mat");

    const Image image = Render(wall);
    int outlined = 0;

    for (uint32_t row = 0; row < image.Height; ++row)
        for (uint32_t x = 0; x < image.Width; ++x)
            if (IsNear(image.At(x, row), glm::ivec3(255, 166, 26), 40))
                ++outlined;

    CHECK(outlined == 0);
}

TEST_CASE_METHOD(ShadowFixture, "A shader's own depth version decides its shadow", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);
    AddLight({ .Type = LightType::Directional, .CastsShadow = true }, glm::vec3(0.0f), glm::vec3(1.0f, -1.0f, 0.0f));

    const auto shadowAt = [this](const Image& image, const glm::vec3& where)
    {
        return LinearBrightness(PixelAt(image, where)) < LinearBrightness(PixelAt(image, kInLight)) / 2;
    };

    SECTION("Its PSDepth discards everything, so it casts no shadow but still draws")
    {
        Project.Write("ghost.hlsl", R"(
#include "crimson/transforms.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return WorldToClip(ObjectToWorldPosition(position)); }
float4 PSMain() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }
void PSDepth() { clip(-1.0); }
)");
        Project.Write("ghost.mat", R"({ "shader": "ghost.hlsl" })");
        Assets->Refresh();
        UseMaterial(Roof, *Assets, "ghost.mat");

        const Image image = Render();
        CHECK_FALSE(shadowAt(image, kInShadow));

        const glm::ivec4 roof = PixelAt(image, glm::vec3(0.0f, 1.0f, 0.0f));
        CAPTURE(roof);
        CHECK(roof.r > 200);
        CHECK(roof.g < 50);
    }

    SECTION("Without PSDepth, its VSMain places the shadow")
    {
        Project.Write("moved.hlsl", R"(
#include "crimson/transforms.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return WorldToClip(ObjectToWorldPosition(position) + float3(3.0, 0.0, 0.0)); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
        Project.Write("moved.mat", R"({ "shader": "moved.hlsl" })");
        Assets->Refresh();
        UseMaterial(Roof, *Assets, "moved.mat");

        const Image image = Render();
        CHECK_FALSE(shadowAt(image, kInShadow));

        // Its shadow falls from x 3.5 to 4.5, and the moved mesh hides the ground up to about 4 from the camera above
        CHECK(shadowAt(image, glm::vec3(4.3f, 0.0f, 0.0f)));
    }
}

namespace
{
    // What the shaders below are compared with: one light from the side, some ambient, and every value set by hand,
    // as the mask shaders' defaults differ from S_Lit's
    constexpr std::string_view kSharedValues =
        R"("Color": [0.8, 0.4, 0.2, 1], "Metallic": 0.5, "Roughness": 0.4, "OcclusionStrength": 0.5, "EmissiveColor": [0.1, 0, 0], "EmissiveStrength": 1)";

    constexpr std::string_view kLit = "engine/shaders/S_Lit.hlsl";
    constexpr std::string_view kLitMask = "engine/shaders/S_LitMask.hlsl";

    std::string Properties(std::string_view extra = "")
    {
        return std::string("{ ") + std::string(kSharedValues) + (extra.empty() ? "" : ", ") + std::string(extra) + " }";
    }

    void CheckSamePixels(const std::array<glm::ivec4, 2>& actual, const std::array<glm::ivec4, 2>& expected)
    {
        for (size_t i = 0; i < actual.size(); ++i)
        {
            CAPTURE(i, actual[i].r, actual[i].g, actual[i].b, expected[i].r, expected[i].g, expected[i].b);
            CHECK(IsNear(actual[i], glm::ivec3(expected[i]), 1));
        }
    }

    struct SharedFixture : MaterialFixture
    {
        SharedFixture()
        {
            Lighting.AmbientColor = glm::vec3(0.2f);
            AddLight({ .Type = LightType::Directional }, glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, -1.0f));
        }
    };
}

TEST_CASE_METHOD(SharedFixture, "S_LitMask lights a surface like S_Lit given the same values", "[pbr]")
{
    SECTION("Values only")
    {
        const auto lit = RenderWall(Properties(), "{}", std::string(kLit));
        const auto mask = RenderWall(Properties(), "{}", std::string(kLitMask));

        CheckSamePixels(mask, lit);
    }

    SECTION("With maps holding the same values")
    {
        WriteDataTexture("metallic_roughness.bmp", Rgb{ 0, 255, 128 });
        WriteDataTexture("occlusion.bmp", Rgb{ 128, 0, 0 });
        WriteDataTexture("mask.bmp", Rgb{ 128, 128, 0 });
        WriteDataTexture("lean.bmp", Rgb{ 218, 128, 218 });
        Project.Write("emissive.bmp", MakeBmp(1, 1, { Rgb{ 255, 128, 0 } }));

        const auto lit = RenderWall(Properties(), R"({ "MetallicRoughnessMap": "metallic_roughness.bmp", "OcclusionMap": "occlusion.bmp",
            "NormalMap": "lean.bmp", "EmissiveMap": "emissive.bmp" })", std::string(kLit));
        const auto mask = RenderWall(Properties(), R"({ "MaskMap": "mask.bmp", "NormalMap": "lean.bmp", "EmissiveMap": "emissive.bmp" })",
            std::string(kLitMask));

        CheckSamePixels(mask, lit);
    }
}

// A map of one color reads the same at any UV or projection, and on a wall facing +Z the projected normals blend back to the wall's own.
// So what's left to differ is how each shader handles the mask, occlusion and emissive values.
TEST_CASE_METHOD(SharedFixture, "The mask-map variants light a flat wall like S_LitMask, given maps of one color", "[pbr]")
{
    WriteDataTexture("mask.bmp", Rgb{ 128, 128, 0 });
    WriteDataTexture("height.bmp", Rgb{ 0, 0, 0 });
    Project.Write("color.bmp", MakeBmp(1, 1, { Rgb{ 200, 100, 50 } }));
    Project.Write("emissive.bmp", MakeBmp(1, 1, { Rgb{ 255, 128, 0 } }));

    const std::string textures = R"("MaskMap": "mask.bmp", "ColorMap": "color.bmp", "EmissiveMap": "emissive.bmp")";
    const auto expected = RenderWall(Properties(), "{ " + textures + " }", std::string(kLitMask));

    for (const char* shader : { "S_LitMaskParallax", "S_LitMaskTriplanar", "S_LitMaskBiplanar" })
    {
        DYNAMIC_SECTION(shader)
        {
            const std::string extra = std::string(shader) == "S_LitMaskParallax" ? R"(, "HeightMap": "height.bmp")" : "";
            const auto actual = RenderWall(Properties(), "{ " + textures + extra + " }", fmt::format("engine/shaders/{}.hlsl", shader));

            CheckSamePixels(actual, expected);
        }
    }
}

TEST_CASE_METHOD(MaterialFixture, "Triplanar and biplanar project along the axis the surface faces, without UVs", "[pbr]")
{
    Lighting.AmbientColor = glm::vec3(0.5f);

    // Red, then green: the Z projection's U is +x * ProjectionTiling, so x = 0.5 reads U 0.25, and x = -0.5 wraps to U 0.75
    Project.Write("halves.bmp", MakeBmp(2, 1, { Rgb{ 255, 0, 0 }, Rgb{ 0, 255, 0 } }));

    for (const char* shader : { "S_LitMaskTriplanar", "S_LitMaskBiplanar" })
    {
        DYNAMIC_SECTION(shader)
        {
            const Entity wall = AddWall(R"({ "Metallic": 0, "Roughness": 1, "ProjectionTiling": 0.5 })", R"({ "ColorMap": "halves.bmp" })",
                fmt::format("engine/shaders/{}.hlsl", shader), "wall.obj");
            const Image image = Render();

            const glm::ivec4 right = PixelAt(image, glm::vec3(0.5f, 0.0f, 0.0f));
            const glm::ivec4 left = PixelAt(image, glm::vec3(-0.5f, 0.0f, 0.0f));

            CAPTURE(right.r, right.g, left.r, left.g);
            CHECK(right.r > right.g * 3);
            CHECK(left.g > left.r * 3);
        }
    }
}

TEST_CASE_METHOD(MaterialFixture, "A parallax height map shows the surface sunk away from the camera", "[pbr]")
{
    Lighting.AmbientColor = glm::vec3(0.5f);

    // Red rises with U, so a larger U there reads as a brighter red
    std::vector<Rgb> ramp;
    for (unsigned char i = 0; i < 8; ++i)
        ramp.push_back(Rgb{ static_cast<unsigned char>(i * 32), 0, 0 });

    Project.Write("ramp.bmp", MakeBmp(8, 1, ramp));
    WriteDataTexture("deep.bmp", Rgb{ 0, 0, 0 });

    // The camera sees this point from its left, so a sunken surface shows more of what lies to the right
    const auto redAt = [&](const char* strength)
    {
        const std::string properties = std::string(R"({ "Metallic": 0, "Roughness": 1, "ParallaxHeightStrength": )") + strength + " }";
        const Entity wall = AddWall(properties, R"({ "ColorMap": "ramp.bmp", "HeightMap": "deep.bmp" })", "engine/shaders/S_LitMaskParallax.hlsl");
        const int red = PixelAt(Render(), glm::vec3(0.6f, 0.0f, 0.0f)).r;
        World.DestroyEntity(wall);
        return red;
    };

    const int flat = redAt("0");
    const int sunk = redAt("0.2");

    CAPTURE(flat, sunk);
    CHECK(sunk > flat);
}
