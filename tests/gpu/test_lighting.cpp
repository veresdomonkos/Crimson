#include <catch2/catch_test_macros.hpp>

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
        const int shadow = Brightness(PixelAt(image, kInShadow));
        const int light = Brightness(PixelAt(image, kInLight));

        CAPTURE(shadow, light);
        CHECK(shadow < light / 2);
    }

    SECTION("A mesh that doesn't cast shadows leaves none")
    {
        Roof.GetComponent<MeshRendererComponent>().CastsShadows = false;

        const Image image = Render();
        const int shadow = Brightness(PixelAt(image, kInShadow));
        const int light = Brightness(PixelAt(image, kInLight));

        CAPTURE(shadow, light);
        CHECK(std::abs(shadow - light) <= 6);
    }
}

TEST_CASE_METHOD(ShadowFixture, "A light that doesn't cast shadows goes through meshes", "[lighting]")
{
    AddLight({ .Type = LightType::Directional, .CastsShadow = false }, glm::vec3(0.0f), glm::vec3(1.0f, -1.0f, 0.0f));

    const Image image = Render();
    CHECK(std::abs(Brightness(PixelAt(image, kInShadow)) - Brightness(PixelAt(image, kInLight))) <= 6);
}

TEST_CASE_METHOD(ShadowFixture, "A spot light casts a shadow", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);

    AddLight({ .Type = LightType::Spot, .Intensity = 8.0f, .Range = 20.0f, .InnerConeDegrees = 60.0f, .OuterConeDegrees = 70.0f, .CastsShadow = true },
        glm::vec3(-1.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    const Image image = Render();
    const int shadow = Brightness(PixelAt(image, kInShadow));
    const int light = Brightness(PixelAt(image, glm::vec3(-3.0f, 0.0f, 0.0f)));

    CAPTURE(shadow, light);
    CHECK(shadow < light);
}

TEST_CASE_METHOD(ShadowFixture, "A point light casts a shadow", "[lighting]")
{
    Lighting.AmbientColor = glm::vec3(0.1f);

    AddLight({ .Type = LightType::Point, .Intensity = 4.0f, .Range = 20.0f, .CastsShadow = true },
        glm::vec3(-1.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    const Image image = Render();
    const int light = Brightness(PixelAt(image, glm::vec3(-3.0f, 0.0f, 0.0f)));

    // The point light has six views, so these two fall on different ones: straight down, and to the side
    for (const float x : { 0.9f, 1.1f })
    {
        const int shadow = Brightness(PixelAt(image, glm::vec3(x, 0.0f, 0.0f)));

        CAPTURE(x, shadow, light);
        CHECK(shadow < light / 2);
    }

    const int seam = Brightness(PixelAt(image, kInShadow));
    INFO("seam brightness " << seam << " light " << light);
    CHECK(seam < light / 2);
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
