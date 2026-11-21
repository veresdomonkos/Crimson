#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string_view>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "support/fake_gpu.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::FakeRenderer;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;
using Catch::Approx;

namespace
{
    constexpr std::string_view kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";
    constexpr std::string_view kSlotsObj = "mtllib slots.mtl\nv 0 0 0\nv 1 0 0\nv 0 2 0\nusemtl Paint\nf 1 2 3\n";
    constexpr std::string_view kSlotsMtl = "newmtl Paint\nKd 1 0 0\n";
    constexpr std::string_view kMaterial = R"({ "shader": "engine/shaders/S_Lit.hlsl" })";
    constexpr std::string_view kBrokenMaterial = R"({ "shader": "engine/shaders/S_Nothing.hlsl" })";

    // The project's files, there before the assets are scanned
    struct ProjectFiles
    {
        ProjectFiles()
        {
            Temp.Write("tri.obj", kTriangleObj);
            Temp.Write("slots.obj", kSlotsObj);
            Temp.Write("slots.mtl", kSlotsMtl);
            Temp.Write("mine.mat", kMaterial);
            Temp.Write("other.mat", kMaterial);
            Temp.Write("broken.mat", kBrokenMaterial);
            Temp.Write("tone_map.mat", kMaterial);
            Temp.Write("outline.mat", kMaterial);
        }

        TempFolder Temp;
    };

    struct Fixture : ProjectFiles
    {
        explicit Fixture(ShadowAtlasSettings atlas = {}) : Subject(Gpu, Renderer, Assets, atlas) {}

        FakeGpuResourceManager Gpu;
        FakeRenderer Renderer;
        AssetManager Assets{ Gpu, Temp.Path() };

        // The renderer's own materials by id, as a project's would be
        MeshID Triangle = Assets.Find<MeshAsset>("tri.obj");
        MeshID Slots = Assets.Find<MeshAsset>("slots.obj");
        MaterialID Mine = Assets.Find<MaterialAsset>("mine.mat");
        MaterialID ToneMap = Assets.Find<MaterialAsset>("tone_map.mat");
        MaterialID Outline = Assets.Find<MaterialAsset>("outline.mat");

        SceneRenderer Subject;
        Scene World;
        FrameLighting Lighting;
        PassInfo MainPass{ .Target = RenderTargetHandle{ 50, 1 } };

        [[nodiscard]] MaterialHandle Handle(MaterialID id) { return *Assets.Get(id); }

        std::vector<const PassRecord*> Record(Entity outlined = {})
        {
            World.UpdateWorldTransforms();

            FrameRecorder frame = Renderer.BeginRecording();
            Subject.BuildLighting(World, frame.Lighting());
            Subject.Record(frame, World, MainPass, outlined);

            Lighting = frame.Lighting();
            Renderer.Submit(std::move(frame));

            std::vector<const PassRecord*> passes;
            for (const PassRecord& pass : Renderer.Submitted->GetPasses())
                passes.push_back(&pass);
            return passes;
        }

        // What the one mesh in the scene drew with, one entry per draw
        std::vector<MaterialHandle> DrawnMaterials()
        {
            std::vector<MaterialHandle> materials;
            for (const DrawItem& draw : Record().back()->GetDraws())
                materials.push_back(draw.Material);
            return materials;
        }
    };

    // Draws the triangle with the project material "mine", unless told otherwise
    Entity AddMesh(Fixture& fixture, const char* name, Entity parent = {}, bool castsShadows = true)
    {
        const Entity entity = fixture.World.CreateEntity(name, parent);
        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.CastsShadows = castsShadows;
        renderer.MeshAsset = fixture.Triangle;
        renderer.MaterialAssets = { fixture.Mine };
        return entity;
    }

    Entity AddLight(Scene& scene, LightType type, bool castsShadow)
    {
        const Entity entity = scene.CreateEntity("Light");
        entity.AddComponent<LightComponent>(LightComponent{ .Type = type, .CastsShadow = castsShadow });
        return entity;
    }
}

TEST_CASE("The shadow atlas is created, handed to the renderer and freed", "[scene renderer]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;
    FakeRenderer renderer;

    {
        AssetManager assets(gpu, temp.Path());
        const SceneRenderer sceneRenderer(gpu, renderer, assets);
        CHECK(sceneRenderer.GetShadowAtlas());
        CHECK(renderer.ShadowAtlas == sceneRenderer.GetShadowAtlas());
        CHECK(gpu.LiveRenderTargets == 1);
    }

    CHECK(gpu.NothingAlive());
}

TEST_CASE("Outline resources are freed too", "[scene renderer]")
{
    const TempFolder temp;
    temp.Write("outline.mat", kMaterial);
    FakeGpuResourceManager gpu;
    FakeRenderer renderer;

    {
        AssetManager assets(gpu, temp.Path());
        SceneRenderer sceneRenderer(gpu, renderer, assets);
        sceneRenderer.SetOutlineMaterial(assets.Find<MaterialAsset>("outline.mat"), 64, 64);
        CHECK(gpu.LiveRenderTargets == 2);
        CHECK(gpu.LiveVertexBuffers == 1);
    }

    CHECK(gpu.NothingAlive());
}

TEST_CASE("Lighting comes from the active lights' transforms", "[scene renderer]")
{
    Fixture fixture;

    SECTION("A light points along its -Z axis, from where its parents put it")
    {
        const Entity parent = fixture.World.CreateEntity("Parent");
        parent.GetComponent<TransformComponent>().Position = glm::vec3(1.0f, 0.0f, 0.0f);

        const Entity light = fixture.World.CreateEntity("Light", parent);
        light.AddComponent<LightComponent>();
        light.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, 2.0f, 0.0f);
        light.GetComponent<TransformComponent>().RotationDegrees = glm::vec3(-90.0f, 0.0f, 0.0f);

        fixture.Record();

        REQUIRE(fixture.Lighting.Lights.size() == 1);
        CHECK(fixture.Lighting.Lights[0].Position == glm::vec3(1.0f, 2.0f, 0.0f));
        CHECK(fixture.Lighting.Lights[0].Direction.y == Approx(-1.0f));
    }

    SECTION("Cone angles turn into radians")
    {
        const Entity light = fixture.World.CreateEntity("Light");
        light.AddComponent<LightComponent>(LightComponent{ .Type = LightType::Spot, .InnerConeDegrees = 90.0f, .OuterConeDegrees = 180.0f });

        fixture.Record();

        REQUIRE(fixture.Lighting.Lights.size() == 1);
        CHECK(fixture.Lighting.Lights[0].InnerConeAngle == Approx(glm::half_pi<float>()));
        CHECK(fixture.Lighting.Lights[0].OuterConeAngle == Approx(glm::pi<float>()));
    }

    SECTION("An inactive light is left out")
    {
        const Entity light = AddLight(fixture.World, LightType::Point, true);
        light.GetComponent<NameComponent>().Active = false;

        fixture.Record();

        CHECK(fixture.Lighting.Lights.empty());
        CHECK(fixture.Lighting.Shadows.empty());
    }

    SECTION("It doesn't pile up between frames")
    {
        AddLight(fixture.World, LightType::Spot, true);

        fixture.Record();
        fixture.Record();

        CHECK(fixture.Lighting.Lights.size() == 1);
        CHECK(fixture.Lighting.Shadows.size() == 1);
    }
}

TEST_CASE("One pass draws every shadow view, then comes the main pass", "[scene renderer]")
{
    Fixture fixture;

    AddLight(fixture.World, LightType::Directional, true);
    AddLight(fixture.World, LightType::Point, true);
    AddLight(fixture.World, LightType::Spot, false);

    AddMesh(fixture, "Caster");
    AddMesh(fixture, "Non-caster", {}, false);

    const std::vector<const PassRecord*> passes = fixture.Record();
    const std::span<const FrameView> views = fixture.Renderer.Submitted->GetViews();

    REQUIRE(passes.size() == 2);
    REQUIRE(fixture.Lighting.Shadows.size() == 1 + 6);

    // The pass clears the whole atlas, its views keep to their tiles
    const PassRecord& shadows = *passes[0];
    CHECK(std::string_view(shadows.GetInfo().Name) == "Shadows");
    CHECK_FALSE(shadows.GetInfo().Viewport);
    CHECK(HasClearFlag(shadows.GetInfo().Clear.Flags, ClearFlags::Depth));

    // The caster once in every view, in the order of the views
    REQUIRE(shadows.GetDraws().size() == fixture.Lighting.Shadows.size());

    for (size_t i = 0; i < shadows.GetDraws().size(); ++i)
    {
        CAPTURE(i);
        const DrawItem& draw = shadows.GetDraws()[i];
        const ShadowView& shadow = fixture.Lighting.Shadows[i];

        // The caster's own material, in its depth version, so its shader decides its shape in the shadow too
        CHECK(draw.Material == fixture.Handle(fixture.Mine));
        CHECK(draw.Variant == ShaderVariant::Depth);

        REQUIRE(draw.View.Index() < views.size());
        CHECK(draw.View != shadows.GetView());
        CHECK(views[draw.View.Index()].View.ViewProj == shadow.Shadow.ViewProj);

        REQUIRE(views[draw.View.Index()].Viewport);
        CHECK(views[draw.View.Index()].Viewport->X == shadow.Viewport.X);
        CHECK(views[draw.View.Index()].Viewport->Y == shadow.Viewport.Y);
        CHECK(views[draw.View.Index()].Viewport->Width == shadow.Viewport.Width);
    }

    const PassRecord& main = *passes.back();
    CHECK(main.GetInfo().Target == fixture.MainPass.Target);
    REQUIRE(main.GetDraws().size() == 2);
    CHECK(main.GetDraws()[0].Material == fixture.Handle(fixture.Mine));
    CHECK(main.GetDraws()[1].Material == fixture.Handle(fixture.Mine));
    CHECK(main.GetDraws()[0].Variant == ShaderVariant::Main);
    CHECK(main.GetDraws()[0].View == main.GetView());
}

TEST_CASE("Without shadowed lights there is no shadow pass", "[scene renderer]")
{
    Fixture fixture;

    AddLight(fixture.World, LightType::Point, false);
    AddMesh(fixture, "Caster");

    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 1);
    CHECK(std::string_view(passes[0]->GetInfo().Name) == "Scene");
}

TEST_CASE("Shadowed lights are limited by the atlas, not by the frame's passes", "[scene renderer]")
{
    // 256 tiles, far more than a frame has passes
    const LogCounter warnings(LogLevel::Warn, "[Shadows]");
    Fixture fixture(ShadowAtlasSettings{ .AtlasSize = 8192, .TileSize = 512 });

    fixture.Subject.SetToneMapMaterial(fixture.ToneMap, 64, 64);
    fixture.Subject.SetOutlineMaterial(fixture.Outline, 64, 64);

    // Six views each. 42 fill 252 of the tiles, and the last two don't fit in the 4 that are left.
    constexpr uint32_t kLights = 44;
    constexpr uint32_t kShadowed = 42;
    for (uint32_t i = 0; i < kLights; ++i)
        AddLight(fixture.World, LightType::Point, true);

    const Entity entity = AddMesh(fixture, "Mesh");

    const LogCounter noRoom(LogLevel::Warn, "No room for more shadows");
    const std::vector<const PassRecord*> passes = fixture.Record(entity);
    fixture.Record(entity);

    REQUIRE(fixture.Lighting.Shadows.size() == kShadowed * 6);

    for (uint32_t i = 0; i < kLights; ++i)
    {
        CAPTURE(i);
        CHECK((fixture.Lighting.Lights[i].ShadowIndex >= 0) == (i < kShadowed));
    }

    REQUIRE(passes.size() == 5);
    CHECK(std::string_view(passes[0]->GetInfo().Name) == "Shadows");
    CHECK(std::string_view(passes[1]->GetInfo().Name) == "Outline Mask");
    CHECK(std::string_view(passes[2]->GetInfo().Name) == "Scene");
    CHECK(std::string_view(passes[3]->GetInfo().Name) == "Tone Mapping");
    CHECK(std::string_view(passes[4]->GetInfo().Name) == "Outline");

    CHECK(passes[0]->GetDraws().size() == kShadowed * 6);

    // Only the full atlas is worth a warning, said when it starts and not on every frame
    CHECK(noRoom.Count() == 1);
    CHECK(warnings.Count() == 1);
}

TEST_CASE("64 point lights are all shadowed in an atlas with room for them", "[scene renderer]")
{
    // 576 tiles for 384 views
    Fixture fixture(ShadowAtlasSettings{ .AtlasSize = 12288, .TileSize = 512 });

    constexpr uint32_t kLights = 64;
    for (uint32_t i = 0; i < kLights; ++i)
        AddLight(fixture.World, LightType::Point, true);

    AddMesh(fixture, "Mesh");

    const LogCounter warnings(LogLevel::Warn, "[Shadows]");
    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 2);
    CHECK(fixture.Lighting.Shadows.size() == kLights * 6);
    CHECK(passes[0]->GetDraws().size() == kLights * 6);

    for (const Light& light : fixture.Lighting.Lights)
        CHECK(light.ShadowIndex >= 0);

    CHECK(warnings.Count() == 0);
}

TEST_CASE("Only active, loaded meshes are drawn", "[scene renderer]")
{
    Fixture fixture;

    AddMesh(fixture, "Drawn");

    const Entity inactive = AddMesh(fixture, "Inactive");
    inactive.GetComponent<NameComponent>().Active = false;

    const Entity parent = fixture.World.CreateEntity("Inactive parent");
    parent.GetComponent<NameComponent>().Active = false;
    AddMesh(fixture, "Inactive child", parent);

    const Entity notLoaded = AddMesh(fixture, "Not loaded");
    fixture.Temp.Write("gone.obj", kTriangleObj);
    fixture.Assets.Refresh();
    const MeshID gone = fixture.Assets.Find<MeshAsset>("gone.obj");
    notLoaded.GetComponent<MeshRendererComponent>().MeshAsset = gone;
    REQUIRE(fixture.Assets.Delete(gone));

    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 1);
    CHECK(passes[0]->GetDraws().size() == 1);
}

TEST_CASE("A draw's object is its world transform", "[scene renderer]")
{
    Fixture fixture;

    const Entity entity = AddMesh(fixture, "Moved");
    entity.GetComponent<TransformComponent>().Position = glm::vec3(1.0f, 2.0f, 3.0f);

    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 1);
    REQUIRE(passes[0]->GetDraws().size() == 1);

    const GpuObject& object = fixture.Renderer.Submitted->GetObjects()[passes[0]->GetDraws()[0].Object.Index()];
    CHECK(glm::vec3(object.Transform[3]) == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("Outlining adds a mask pass and a full screen draw", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetOutlineMaterial(fixture.Outline, 64, 64);

    const Entity parent = AddMesh(fixture, "Parent");
    AddMesh(fixture, "Child", parent);
    const Entity other = AddMesh(fixture, "Other");

    SECTION("Nothing selected")
    {
        const std::vector<const PassRecord*> passes = fixture.Record();

        REQUIRE(passes.size() == 1);
        CHECK(passes[0]->GetDraws().size() == 3);
    }

    SECTION("The selected entity's children are outlined with it")
    {
        const std::vector<const PassRecord*> passes = fixture.Record(parent);

        REQUIRE(passes.size() == 3);

        const PassRecord& mask = *passes[0];
        REQUIRE(mask.GetDraws().size() == 2);
        CHECK(mask.GetDraws()[0].Material == fixture.Handle(fixture.Mine));
        CHECK(mask.GetDraws()[0].Variant == ShaderVariant::Depth);

        const PassRecord& main = *passes[1];
        REQUIRE(main.GetDraws().size() == 3);
        CHECK(main.GetInfo().Target == fixture.MainPass.Target);

        const PassRecord& outline = *passes[2];
        REQUIRE(outline.GetDraws().size() == 1);
        CHECK(outline.GetDraws()[0].Material == fixture.Handle(fixture.Outline));
        CHECK(outline.GetInfo().Target == fixture.MainPass.Target);
    }

    SECTION("A selected entity without a mesh of its own, or below it, adds nothing")
    {
        const Entity empty = fixture.World.CreateEntity("Empty");
        CHECK(fixture.Record(empty).size() == 1);
    }

    SECTION("An entity that was destroyed while selected adds nothing")
    {
        fixture.World.DestroyEntity(other);
        CHECK(fixture.Record(other).size() == 1);
    }
}

TEST_CASE("Tone mapping renders the scene to an HDR target first", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(fixture.ToneMap, 64, 64);
    fixture.Subject.SetOutlineMaterial(fixture.Outline, 64, 64);

    const Entity entity = AddMesh(fixture, "Mesh");

    SECTION("The output only gets the tone mapped picture")
    {
        const std::vector<const PassRecord*> passes = fixture.Record();
        REQUIRE(passes.size() == 2);

        CHECK(passes[0]->GetInfo().Target != fixture.MainPass.Target);
        CHECK(passes[0]->GetDraws().size() == 1);

        CHECK(passes[1]->GetInfo().Target == fixture.MainPass.Target);
        REQUIRE(passes[1]->GetDraws().size() == 1);
        CHECK(passes[1]->GetDraws()[0].Material == fixture.Handle(fixture.ToneMap));
    }

    SECTION("The outline goes after it")
    {
        const std::vector<const PassRecord*> passes = fixture.Record(entity);
        REQUIRE(passes.size() == 4);

        CHECK(passes[0]->GetDraws()[0].Variant == ShaderVariant::Depth);
        CHECK(passes[2]->GetDraws()[0].Material == fixture.Handle(fixture.ToneMap));
        CHECK(passes[3]->GetDraws()[0].Material == fixture.Handle(fixture.Outline));
        CHECK(passes[3]->GetInfo().Target == fixture.MainPass.Target);
    }

    SECTION("Exposure and the tone mapper reach the material")
    {
        fixture.Subject.SetToneMapping(2.0f, ToneMapper::None);
        CHECK(fixture.Gpu.Materials.at(fixture.Handle(fixture.ToneMap).GetRaw()).Properties.contains("Exposure"));
        CHECK(fixture.Gpu.Materials.at(fixture.Handle(fixture.ToneMap).GetRaw()).Properties.contains("ToneMapper"));
    }
}

TEST_CASE("A tone map material that failed to load leaves the scene going straight to the target", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(MaterialID{}, 64, 64);
    CHECK(fixture.Gpu.LiveRenderTargets == 1);

    AddMesh(fixture, "Mesh");

    const std::vector<const PassRecord*> passes = fixture.Record();
    REQUIRE(passes.size() == 1);
    CHECK(passes[0]->GetInfo().Target == fixture.MainPass.Target);
}

TEST_CASE("Without an outline material, selecting draws nothing extra", "[scene renderer]")
{
    Fixture fixture;

    SECTION("Never set")
    {
        const Entity entity = AddMesh(fixture, "Selected");
        CHECK(fixture.Record(entity).size() == 1);
    }

    SECTION("It failed to load")
    {
        fixture.Subject.SetOutlineMaterial(MaterialID{}, 64, 64);
        CHECK(fixture.Gpu.LiveRenderTargets == 1);

        const Entity entity = AddMesh(fixture, "Selected");
        CHECK(fixture.Record(entity).size() == 1);
    }
}

TEST_CASE("Passes are named and say what they sample", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(fixture.ToneMap, 64, 64);
    fixture.Subject.SetOutlineMaterial(fixture.Outline, 64, 64);

    AddLight(fixture.World, LightType::Spot, true);
    const Entity entity = AddMesh(fixture, "Mesh");

    const std::vector<const PassRecord*> passes = fixture.Record(entity);
    REQUIRE(passes.size() == 5);

    CHECK(std::string_view(passes[0]->GetInfo().Name) == "Shadows");
    CHECK(std::string_view(passes[1]->GetInfo().Name) == "Outline Mask");
    CHECK(std::string_view(passes[2]->GetInfo().Name) == "Scene");
    CHECK(std::string_view(passes[3]->GetInfo().Name) == "Tone Mapping");
    CHECK(std::string_view(passes[4]->GetInfo().Name) == "Outline");

    CHECK(passes[2]->GetInfo().Reads[0] == fixture.Subject.GetShadowAtlas());
    CHECK(passes[3]->GetInfo().Reads[0] == fixture.Gpu.GetColorAttachment(passes[2]->GetInfo().Target, 0));
    CHECK(passes[4]->GetInfo().Reads[0] == fixture.Gpu.GetDepthAttachment(passes[1]->GetInfo().Target).value());
}

TEST_CASE("The scene pass samples the shadow atlas next to what the main pass samples", "[scene renderer]")
{
    constexpr TextureHandle kEnvironment{ 77, 1 };

    Fixture fixture;
    fixture.MainPass.Reads = { kEnvironment };
    AddMesh(fixture, "Mesh");

    const std::vector<const PassRecord*> passes = fixture.Record();
    REQUIRE(passes.size() == 1);

    CHECK(passes[0]->GetInfo().Reads[0] == kEnvironment);
    CHECK(passes[0]->GetInfo().Reads[1] == fixture.Subject.GetShadowAtlas());
}

TEST_CASE("An empty slot draws the default, and a material made for it once there is one, without anything resolving", "[scene renderer]")
{
    Fixture fixture;

    const Entity entity = fixture.World.CreateEntity("Painted");
    entity.AddComponent<MeshRendererComponent>().MeshAsset = fixture.Slots;

    const MaterialHandle defaultMaterial = fixture.Handle(slot_materials::DefaultMaterial(fixture.Assets));
    CHECK(fixture.DrawnMaterials() == std::vector{ defaultMaterial });

    fixture.Temp.Write("materials/Paint.mat", kMaterial);
    fixture.Assets.Refresh();

    const MaterialID paint = slot_materials::Find(fixture.Assets, fixture.Slots, "Paint");
    REQUIRE(paint);
    CHECK(fixture.DrawnMaterials() == std::vector{ fixture.Handle(paint) });

    SECTION("A chosen material stays chosen")
    {
        entity.GetComponent<MeshRendererComponent>().MaterialAssets = { fixture.Mine };
        CHECK(fixture.DrawnMaterials() == std::vector{ fixture.Handle(fixture.Mine) });
    }

    SECTION("Until the material made for it goes")
    {
        REQUIRE(fixture.Assets.Delete(paint));
        CHECK(fixture.DrawnMaterials() == std::vector{ defaultMaterial });
    }
}

TEST_CASE("A material made again draws with its new handle", "[scene renderer]")
{
    Fixture fixture;
    AddMesh(fixture, "Mesh");

    const MaterialHandle before = fixture.DrawnMaterials().front();
    CHECK(before == fixture.Handle(fixture.Mine));

    MaterialData data = *fixture.Assets.GetData(fixture.Mine);
    data.Shader = fixture.Assets.Find<ShaderAsset>("engine/shaders/S_Unlit.hlsl");
    REQUIRE(fixture.Assets.Apply(fixture.Mine, data) == AssetUpdate::Replaced);

    const std::vector<MaterialHandle> after = fixture.DrawnMaterials();
    REQUIRE(after.size() == 1);
    CHECK(after[0] == fixture.Handle(fixture.Mine));
    CHECK(after[0] != before);
}

TEST_CASE("A missing material, and one that doesn't load, draw as the missing one", "[scene renderer]")
{
    Fixture fixture;
    const Entity entity = AddMesh(fixture, "Mesh");
    auto& renderer = entity.GetComponent<MeshRendererComponent>();

    const MaterialHandle missing = fixture.Handle(slot_materials::MissingMaterial(fixture.Assets));
    CHECK(missing != fixture.Handle(slot_materials::DefaultMaterial(fixture.Assets)));

    SECTION("One that was deleted")
    {
        REQUIRE(fixture.Assets.Delete(fixture.Mine));
        CHECK(fixture.DrawnMaterials() == std::vector{ missing });
    }

    SECTION("One that exists but doesn't load")
    {
        renderer.MaterialAssets = { fixture.Assets.Find<MaterialAsset>("broken.mat") };
        CHECK(fixture.DrawnMaterials() == std::vector{ missing });
    }

    SECTION("In the shadows and the outline mask too")
    {
        renderer.MaterialAssets = { fixture.Assets.Find<MaterialAsset>("broken.mat") };
        AddLight(fixture.World, LightType::Directional, true);
        fixture.Subject.SetOutlineMaterial(fixture.Outline, 64, 64);

        const std::vector<const PassRecord*> passes = fixture.Record(entity);
        REQUIRE(passes.size() == 4);

        for (const PassRecord* pass : { passes[0], passes[1], passes[2] })
        {
            REQUIRE(pass->GetDraws().size() == 1);
            CHECK(pass->GetDraws()[0].Material == missing);
        }
    }
}

TEST_CASE("The tone map and outline materials follow a new handle", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(fixture.ToneMap, 64, 64);
    fixture.Subject.SetOutlineMaterial(fixture.Outline, 64, 64);
    fixture.Subject.SetToneMapping(2.0f, ToneMapper::None);

    const Entity entity = AddMesh(fixture, "Mesh");
    const MaterialHandle toneMapBefore = fixture.Handle(fixture.ToneMap);
    const MaterialHandle outlineBefore = fixture.Handle(fixture.Outline);

    for (const MaterialID id : { fixture.ToneMap, fixture.Outline })
    {
        MaterialData data = *fixture.Assets.GetData(id);
        data.Shader = fixture.Assets.Find<ShaderAsset>("engine/shaders/S_Unlit.hlsl");
        REQUIRE(fixture.Assets.Apply(id, data) == AssetUpdate::Replaced);
    }

    const std::vector<const PassRecord*> passes = fixture.Record(entity);
    REQUIRE(passes.size() == 4);

    const MaterialHandle toneMap = passes[2]->GetDraws()[0].Material;
    CHECK(toneMap == fixture.Handle(fixture.ToneMap));
    CHECK(toneMap != toneMapBefore);

    // Set up the way the old one was
    const auto& toneMapRecord = fixture.Gpu.Materials.at(toneMap.GetRaw());
    CHECK(toneMapRecord.Textures.contains("SceneColor"));
    CHECK(toneMapRecord.Properties.contains("Exposure"));

    const MaterialHandle outline = passes[3]->GetDraws()[0].Material;
    CHECK(outline == fixture.Handle(fixture.Outline));
    CHECK(outline != outlineBefore);
    CHECK(fixture.Gpu.Materials.at(outline.GetRaw()).Textures.contains("OutlineMask"));
}

TEST_CASE("A reimported mesh draws from its new buffers", "[scene renderer]")
{
    Fixture fixture;
    AddMesh(fixture, "Mesh");

    const auto drawnBuffer = [&] { return fixture.Record().back()->GetDraws().front().VertexBuffer; };

    const VertexBufferHandle before = drawnBuffer();
    CHECK(before == fixture.Assets.Get(fixture.Triangle)->VertexBuffer);

    nlohmann::json settings = fixture.Assets.GetInfo(fixture.Triangle)->Settings;
    settings["flipV"] = !settings.value("flipV", true);
    REQUIRE(fixture.Assets.Reimport(fixture.Triangle, settings));

    const VertexBufferHandle after = drawnBuffer();
    CHECK(after == fixture.Assets.Get(fixture.Triangle)->VertexBuffer);
    CHECK(after != before);
}
