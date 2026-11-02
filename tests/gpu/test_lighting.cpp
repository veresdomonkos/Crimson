#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "gpu/gpu_test.hpp"
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
        FrameLightingData Lighting;

        std::unique_ptr<AssetManager> Assets;
        std::unique_ptr<SceneRenderer> Subject;

        Fixture()
        {
            Project.Write("wall.obj", kWall);
            Project.Write("floor.obj", kFloor);
            Project.Write("white.mat", R"({ "shader": "engine/shaders/main.hlsl", "properties": { "Color": [1, 1, 1, 1] } })");

            Assets = std::make_unique<AssetManager>(*Gpu().GPUResources, Project.Path());
            Subject = std::make_unique<SceneRenderer>(*Gpu().GPUResources, *Gpu().Renderer, ShadowAtlasSettings{ .AtlasSize = 1024, .TileSize = 256 });

            Subject->SetToneMapMaterial(Assets->LoadMaterial("engine/materials/tonemap.mat"), kSize, kSize);
            Subject->SetToneMapping(1.0f, ToneMapper::None); // so that expected colors only need the sRGB encoding
            Subject->SetShadowMaterial(Assets->LoadMaterial("engine/materials/shadow.mat"));
            Subject->SetOutlineMaterials(Assets->LoadMaterial("engine/materials/outline_mask.mat"), Assets->LoadMaterial("engine/materials/outline.mat"), kSize, kSize);

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
            renderer.MeshAsset = Assets->FindId(mesh);
            renderer.MaterialAssets = { Assets->FindId("white.mat") };
            ResolveMeshRenderer(renderer, *Assets);
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

            const RenderPassInfo main{
                .Target = Target.Handle(),
                .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
                .ClearColor = kClearColor,
                .ViewProj = Camera.GetViewProj(),
                .CameraPosition = Camera.GetPosition()
            };

            RenderOneFrame([&](FrameContext& frame) { Subject->RecordPasses(frame, World, Lighting, main, outlined); }, Lighting);

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

        Entity AddWall(const std::string& properties, const std::string& textures = "{}")
        {
            Project.Write("textured_wall.obj", kTexturedWall);

            const std::string name = "wall" + std::to_string(MaterialCount++) + ".mat";
            Project.Write(name, R"({ "shader": "engine/shaders/main.hlsl", "properties": )" + properties + R"(, "textures": )" + textures + " }");
            Assets->Refresh();

            const Entity entity = World.CreateEntity(name);
            auto& renderer = entity.AddComponent<MeshRendererComponent>();
            renderer.MeshAsset = Assets->FindId("textured_wall.obj");
            renderer.MaterialAssets = { Assets->FindId(name) };
            ResolveMeshRenderer(renderer, *Assets);
            REQUIRE_FALSE(renderer.Resolved.SubMeshes.empty());

            return entity;
        }

        // Data textures hold values and not colors
        void WriteDataTexture(const std::string& name, Rgb color)
        {
            Project.Write(name, MakeBmp(1, 1, { color }));
            Assets->Refresh();
            REQUIRE(Assets->ReimportTexture(Assets->FindId(name), { { "srgb", false } }));
        }

        float RenderCenter()
        {
            return LinearBrightness(PixelAt(Render(), glm::vec3(0.0f)));
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
        Project.Write("nouv.mat", R"({ "shader": "engine/shaders/main.hlsl", "properties": { "Roughness": 1 }, "textures": { "NormalMap": "lean.bmp" } })");
        Assets->Refresh();

        auto& renderer = AddMesh("wall.obj").GetComponent<MeshRendererComponent>();
        renderer.MaterialAssets = { Assets->FindId("nouv.mat") };
        ResolveMeshRenderer(renderer, *Assets);

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
