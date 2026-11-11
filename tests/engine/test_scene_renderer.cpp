#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

#include <crimson/scene/scene_renderer.hpp>

#include "support/fake_gpu.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::FakeRenderer;
using Catch::Approx;

namespace
{
    constexpr MaterialHandle kMeshMaterial{ 100, 1 };
    constexpr MaterialHandle kOutlineMaterial{ 103, 1 };

    Entity AddMesh(Scene& scene, const char* name, Entity parent = {}, bool castsShadows = true)
    {
        const Entity entity = scene.CreateEntity(name, parent);
        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.CastsShadows = castsShadows;
        renderer.Resolved.VertexBuffer = VertexBufferHandle{ 1, 1 };
        renderer.Resolved.IndexBuffer = IndexBufferHandle{ 1, 1 };
        renderer.Resolved.Materials = { kMeshMaterial };
        renderer.Resolved.SubMeshes.push_back({ .IndexCount = 3 });
        return entity;
    }

    Entity AddLight(Scene& scene, LightType type, bool castsShadow)
    {
        const Entity entity = scene.CreateEntity("Light");
        entity.AddComponent<LightComponent>(LightComponent{ .Type = type, .CastsShadow = castsShadow });
        return entity;
    }

    struct Fixture
    {
        FakeGpuResourceManager Gpu;
        FakeRenderer Renderer;
        SceneRenderer Subject{ Gpu, Renderer };
        Scene World;
        FrameLighting Lighting;
        PassInfo MainPass{ .Target = RenderTargetHandle{ 50, 1 } };

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
    };
}

TEST_CASE("The shadow atlas is created, handed to the renderer and freed", "[scene renderer]")
{
    FakeGpuResourceManager gpu;
    FakeRenderer renderer;

    {
        const SceneRenderer sceneRenderer(gpu, renderer);
        CHECK(sceneRenderer.GetShadowAtlas());
        CHECK(renderer.ShadowAtlas == sceneRenderer.GetShadowAtlas());
        CHECK(gpu.LiveRenderTargets == 1);
    }

    CHECK(gpu.NothingAlive());
}

TEST_CASE("Outline resources are freed too", "[scene renderer]")
{
    FakeGpuResourceManager gpu;
    FakeRenderer renderer;

    {
        SceneRenderer sceneRenderer(gpu, renderer);
        sceneRenderer.SetOutlineMaterial(kOutlineMaterial, 64, 64);
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

TEST_CASE("There is one pass per shadow view, then the main pass", "[scene renderer]")
{
    Fixture fixture;

    AddLight(fixture.World, LightType::Directional, true);
    AddLight(fixture.World, LightType::Point, true);
    AddLight(fixture.World, LightType::Spot, false);

    AddMesh(fixture.World, "Caster");
    AddMesh(fixture.World, "Non-caster", {}, false);

    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 1 + 6 + 1);

    for (size_t i = 0; i + 1 < passes.size(); ++i)
    {
        CAPTURE(i);
        REQUIRE(passes[i]->GetDraws().size() == 1);

        // The caster's own material, in its depth version, so its shader decides its shape in the shadow too
        CHECK(passes[i]->GetDraws()[0].Material == kMeshMaterial);
        CHECK(passes[i]->GetDraws()[0].Variant == ShaderVariant::Depth);
    }

    const PassRecord& main = *passes.back();
    CHECK(main.GetInfo().Target == fixture.MainPass.Target);
    REQUIRE(main.GetDraws().size() == 2);
    CHECK(main.GetDraws()[0].Material == kMeshMaterial);
    CHECK(main.GetDraws()[1].Material == kMeshMaterial);
    CHECK(main.GetDraws()[0].Variant == ShaderVariant::Main);
}

TEST_CASE("Only active, loaded meshes are drawn", "[scene renderer]")
{
    Fixture fixture;

    AddMesh(fixture.World, "Drawn");

    const Entity inactive = AddMesh(fixture.World, "Inactive");
    inactive.GetComponent<NameComponent>().Active = false;

    const Entity parent = fixture.World.CreateEntity("Inactive parent");
    parent.GetComponent<NameComponent>().Active = false;
    AddMesh(fixture.World, "Inactive child", parent);

    const Entity notLoaded = AddMesh(fixture.World, "Not loaded");
    notLoaded.GetComponent<MeshRendererComponent>().Resolved.SubMeshes.clear();

    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 1);
    CHECK(passes[0]->GetDraws().size() == 1);
}

TEST_CASE("A draw's object is its world transform", "[scene renderer]")
{
    Fixture fixture;

    const Entity entity = AddMesh(fixture.World, "Moved");
    entity.GetComponent<TransformComponent>().Position = glm::vec3(1.0f, 2.0f, 3.0f);

    const std::vector<const PassRecord*> passes = fixture.Record();

    REQUIRE(passes.size() == 1);
    REQUIRE(passes[0]->GetDraws().size() == 1);

    const GpuObject& object = fixture.Renderer.Submitted->GetObjects()[passes[0]->GetDraws()[0].Object];
    CHECK(glm::vec3(object.Transform[3]) == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("Outlining adds a mask pass and a full screen draw", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetOutlineMaterial(kOutlineMaterial, 64, 64);

    const Entity parent = AddMesh(fixture.World, "Parent");
    AddMesh(fixture.World, "Child", parent);
    const Entity other = AddMesh(fixture.World, "Other");

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
        CHECK(mask.GetDraws()[0].Material == kMeshMaterial);
        CHECK(mask.GetDraws()[0].Variant == ShaderVariant::Depth);

        const PassRecord& main = *passes[1];
        REQUIRE(main.GetDraws().size() == 3);
        CHECK(main.GetInfo().Target == fixture.MainPass.Target);

        const PassRecord& outline = *passes[2];
        REQUIRE(outline.GetDraws().size() == 1);
        CHECK(outline.GetDraws()[0].Material == kOutlineMaterial);
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
    constexpr MaterialHandle kToneMapMaterial{ 104, 1 };

    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(kToneMapMaterial, 64, 64);
    fixture.Subject.SetOutlineMaterial(kOutlineMaterial, 64, 64);

    const Entity entity = AddMesh(fixture.World, "Mesh");

    SECTION("The output only gets the tone mapped picture")
    {
        const std::vector<const PassRecord*> passes = fixture.Record();
        REQUIRE(passes.size() == 2);

        CHECK(passes[0]->GetInfo().Target != fixture.MainPass.Target);
        CHECK(passes[0]->GetDraws().size() == 1);

        CHECK(passes[1]->GetInfo().Target == fixture.MainPass.Target);
        REQUIRE(passes[1]->GetDraws().size() == 1);
        CHECK(passes[1]->GetDraws()[0].Material == kToneMapMaterial);
    }

    SECTION("The outline goes after it")
    {
        const std::vector<const PassRecord*> passes = fixture.Record(entity);
        REQUIRE(passes.size() == 4);

        CHECK(passes[0]->GetDraws()[0].Variant == ShaderVariant::Depth);
        CHECK(passes[2]->GetDraws()[0].Material == kToneMapMaterial);
        CHECK(passes[3]->GetDraws()[0].Material == kOutlineMaterial);
        CHECK(passes[3]->GetInfo().Target == fixture.MainPass.Target);
    }

    SECTION("Exposure and the tone mapper reach the material")
    {
        fixture.Subject.SetToneMapping(2.0f, ToneMapper::None);
        CHECK(fixture.Gpu.Materials.at(kToneMapMaterial.GetRaw()).Properties.contains("Exposure"));
        CHECK(fixture.Gpu.Materials.at(kToneMapMaterial.GetRaw()).Properties.contains("ToneMapper"));
    }
}

TEST_CASE("A tone map material that failed to load leaves the scene going straight to the target", "[scene renderer]")
{
    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(MaterialHandle::Invalid(), 64, 64);
    CHECK(fixture.Gpu.LiveRenderTargets == 1);

    AddMesh(fixture.World, "Mesh");

    const std::vector<const PassRecord*> passes = fixture.Record();
    REQUIRE(passes.size() == 1);
    CHECK(passes[0]->GetInfo().Target == fixture.MainPass.Target);
}

TEST_CASE("Without an outline material, selecting draws nothing extra", "[scene renderer]")
{
    Fixture fixture;

    SECTION("Never set")
    {
        const Entity entity = AddMesh(fixture.World, "Selected");
        CHECK(fixture.Record(entity).size() == 1);
    }

    SECTION("It failed to load")
    {
        fixture.Subject.SetOutlineMaterial(MaterialHandle::Invalid(), 64, 64);
        CHECK(fixture.Gpu.LiveRenderTargets == 1);

        const Entity entity = AddMesh(fixture.World, "Selected");
        CHECK(fixture.Record(entity).size() == 1);
    }
}

TEST_CASE("Passes are named and say what they sample", "[scene renderer]")
{
    constexpr MaterialHandle kToneMapMaterial{ 104, 1 };

    Fixture fixture;
    fixture.Subject.SetToneMapMaterial(kToneMapMaterial, 64, 64);
    fixture.Subject.SetOutlineMaterial(kOutlineMaterial, 64, 64);

    AddLight(fixture.World, LightType::Spot, true);
    const Entity entity = AddMesh(fixture.World, "Mesh");

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
