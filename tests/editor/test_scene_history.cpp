#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_serializer.hpp>
#include <crimson_editor/scene_history.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::SceneHistory;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace
{
    // Apart from the fixture, so the files are there before the asset manager looks
    struct ProjectFiles
    {
        TempFolder Temp;

        ProjectFiles()
        {
            Temp.Write("triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n");
            Temp.Write("thing.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl" })");
            Temp.Write("crate.scene", R"({ "version": 1, "entities": [ { "name": "Lid", "children": [ { "name": "Knob" } ] } ] })");
        }
    };

    struct Fixture : ProjectFiles
    {
        FakeGpuResourceManager Gpu;
        AssetManager Assets{ Gpu, Temp.Path() };
        Scene World;
        SceneEnvironment Environment;
        int EnvironmentChanges = 0;
        SceneHistory History{ World, Assets, Environment, [this] { ++EnvironmentChanges; } };

        SceneData Capture() { return CaptureScene(World, Environment, Assets); }

        Entity Child(Entity parent, size_t index) { return World.GetEntity(parent.GetComponent<RelationshipComponent>().Children.at(index)); }

        static AssetID IdOf(Entity entity) { return entity.GetComponent<NameComponent>().Id; }
    };
}

TEST_CASE_METHOD(Fixture, "A change to an entity is undone and redone", "[scene history]")
{
    const Entity lamp = World.CreateEntity("Lamp");
    lamp.AddComponent<LightComponent>();
    const Entity other = World.CreateEntity("Other");

    const SceneData before = Capture();

    History.Record(lamp);
    lamp.GetComponent<TransformComponent>().Position = glm::vec3(1.0f, 2.0f, 3.0f);
    lamp.GetComponent<LightComponent>().Intensity = 5.0f;
    REQUIRE(History.Commit());

    const SceneData after = Capture();

    REQUIRE(History.Undo());
    CHECK(Capture() == before);
    CHECK_FALSE(History.CanUndo());

    REQUIRE(History.Redo());
    CHECK(Capture() == after);

    // Only that entity was touched, so the others are the same entities as before
    CHECK(other);
    CHECK(lamp);
}

TEST_CASE_METHOD(Fixture, "Nothing changed is no step", "[scene history]")
{
    const Entity entity = World.CreateEntity("Thing");

    History.Record(entity);
    History.RecordEnvironment();
    CHECK_FALSE(History.Commit());
    CHECK_FALSE(History.CanUndo());
}

TEST_CASE_METHOD(Fixture, "A deleted entity comes back with what was under it", "[scene history]")
{
    const ComponentType* health = Assets.Components().RegisterRuntime("health", "Health", {
        FieldInfo{ .Key = "max", .Label = "Max", .Type = FieldType::Int, .Default = int32_t{ 3 } }
    });
    REQUIRE(health);

    World.CreateEntity("First");
    const Entity parent = World.CreateEntity("Parent");
    const Entity light = World.CreateEntity("Light", parent);
    light.AddComponent<LightComponent>(LightComponent{ .Type = LightType::Spot, .Intensity = 3.0f });
    const Entity guard = World.CreateEntity("Guard", parent);
    WriteComponent(*health, guard, { int32_t{ 10 } });
    World.CreateEntity("Last");

    const AssetID parentId = IdOf(parent);
    const AssetID guardId = IdOf(guard);
    const SceneData before = Capture();

    History.RecordDestroy(parent);
    World.DestroyEntity(parent);
    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    CHECK(Capture() == before);

    // In its place, with its ids and components
    const Entity restored = World.GetEntity(World.GetRoots()[1]);
    CHECK(IdOf(restored) == parentId);
    CHECK(Child(restored, 0).GetComponent<LightComponent>().Type == LightType::Spot);
    CHECK(IdOf(Child(restored, 1)) == guardId);
    CHECK(std::get<int32_t>(ReadComponent(*health, Child(restored, 1))[0]) == 10);

    REQUIRE(History.Redo());
    CHECK_FALSE(FindEntity(World, parentId));
    CHECK_FALSE(FindEntity(World, guardId));
    CHECK(World.GetRoots().size() == 2);
}

TEST_CASE_METHOD(Fixture, "A created entity goes on undo and comes back on redo", "[scene history]")
{
    const Entity parent = World.CreateEntity("Parent");

    const Entity created = World.CreateEntity("New", parent);
    created.AddComponent<LightComponent>();
    History.RecordCreated(created);
    REQUIRE(History.Commit());

    const AssetID id = IdOf(created);
    const SceneData after = Capture();

    REQUIRE(History.Undo());
    CHECK_FALSE(FindEntity(World, id));
    CHECK(parent.GetComponent<RelationshipComponent>().Children.empty());

    REQUIRE(History.Redo());
    CHECK(Capture() == after);
    CHECK(FindEntity(World, id).HasComponent<LightComponent>());
}

TEST_CASE_METHOD(Fixture, "A reparented entity goes back where it was", "[scene history]")
{
    const Entity first = World.CreateEntity("First");
    first.GetComponent<TransformComponent>().Position = glm::vec3(10.0f, 0.0f, 0.0f);
    const Entity moved = World.CreateEntity("Moved");
    moved.GetComponent<TransformComponent>().Position = glm::vec3(1.0f, 0.0f, 0.0f);
    World.CreateEntity("Last");

    const SceneData before = Capture();

    History.Record(moved);
    World.SetParent(moved, first);
    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    CHECK(Capture() == before);
    CHECK(World.GetRoots()[1] == moved.GetHandle());
    CHECK(moved.GetComponent<TransformComponent>().Position == glm::vec3(1.0f, 0.0f, 0.0f));

    REQUIRE(History.Redo());
    CHECK(moved.GetComponent<RelationshipComponent>().Parent == first.GetHandle());
}

TEST_CASE_METHOD(Fixture, "Adding and removing a component is undone", "[scene history]")
{
    const Entity entity = World.CreateEntity("Thing");

    SECTION("Added")
    {
        History.Record(entity);
        entity.AddComponent<LightComponent>();
        REQUIRE(History.Commit());

        REQUIRE(History.Undo());
        CHECK_FALSE(entity.HasComponent<LightComponent>());
    }

    SECTION("Removed, it comes back with its values")
    {
        entity.AddComponent<LightComponent>(LightComponent{ .Intensity = 7.0f });

        History.Record(entity);
        entity.RemoveComponent<LightComponent>();
        REQUIRE(History.Commit());

        REQUIRE(History.Undo());
        REQUIRE(entity.HasComponent<LightComponent>());
        CHECK(entity.GetComponent<LightComponent>().Intensity == 7.0f);
    }
}

TEST_CASE_METHOD(Fixture, "A deleted instance comes back with its scene's entities", "[scene history]")
{
    const Entity instance = CreateSceneInstance(Assets.Find<AssetType::Scene>("crate.scene"), World, {}, Assets, {});
    REQUIRE(instance);

    History.RecordDestroy(instance);
    World.DestroyEntity(instance);
    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    REQUIRE(World.GetRoots().size() == 1);

    const Entity restored = World.GetEntity(World.GetRoots()[0]);
    CHECK(restored.HasComponent<SceneInstanceComponent>());
    CHECK(Child(restored, 0).HasComponent<InstancedComponent>());
    CHECK(Child(Child(restored, 0), 0).GetComponent<NameComponent>().Name == "Knob");
}

TEST_CASE_METHOD(Fixture, "Entities inside an instance aren't recorded", "[scene history]")
{
    const Entity instance = CreateSceneInstance(Assets.Find<AssetType::Scene>("crate.scene"), World, {}, Assets, {});
    const Entity lid = Child(instance, 0);

    History.Record(lid);
    lid.GetComponent<NameComponent>().Name = "Changed anyway";
    CHECK_FALSE(History.Commit());
}

TEST_CASE_METHOD(Fixture, "The environment is part of a step", "[scene history]")
{
    History.RecordEnvironment();
    Environment.Exposure = 2.0f;
    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    CHECK(Environment.Exposure == 1.0f);
    CHECK(EnvironmentChanges == 1);

    REQUIRE(History.Redo());
    CHECK(Environment.Exposure == 2.0f);
    CHECK(EnvironmentChanges == 2);
}

TEST_CASE_METHOD(Fixture, "Steps are kept in order, up to a limit", "[scene history]")
{
    const Entity entity = World.CreateEntity("Thing");

    const auto rename = [&](const std::string& name)
    {
        History.Record(entity);
        entity.GetComponent<NameComponent>().Name = name;
        return History.Commit();
    };

    SECTION("A new step drops what could be redone")
    {
        rename("A");
        rename("B");
        REQUIRE(History.Undo());
        CHECK(History.CanRedo());

        rename("C");
        CHECK_FALSE(History.CanRedo());

        REQUIRE(History.Undo());
        CHECK(entity.GetComponent<NameComponent>().Name == "A");
    }

    SECTION("Only the latest ones")
    {
        for (size_t i = 0; i < SceneHistory::kMaxSteps + 10; ++i)
            rename(std::to_string(i));

        size_t undone = 0;
        while (History.Undo())
            ++undone;

        CHECK(undone == SceneHistory::kMaxSteps);
        CHECK(entity.GetComponent<NameComponent>().Name == "9");
    }

    SECTION("Clearing forgets them")
    {
        rename("A");
        History.Clear();
        CHECK_FALSE(History.CanUndo());
        CHECK_FALSE(History.CanRedo());
    }
}

TEST_CASE_METHOD(Fixture, "An assigned asset is undone and resolved again", "[scene history]")
{
    const ComponentType* renderer = Assets.Components().Find<MeshRendererComponent>();
    REQUIRE(renderer);

    const Entity thing = World.CreateEntity("Thing");
    CreateComponent(*renderer, thing, Assets);

    const FieldRef spawn = FindSpawnField(Assets.Components(), AssetType::Mesh);
    REQUIRE(spawn);
    AssignAsset(*spawn.Type, spawn.Field, thing, Assets.Find<AssetType::Mesh>("triangle.obj"), Assets);

    const MaterialID material = Assets.Find<AssetType::Material>("thing.mat");
    const FieldRef drop = FindDropField(Assets.Components(), thing, AssetType::Material);
    REQUIRE(drop);

    const SceneData before = Capture();

    History.Record(thing);
    AssignAsset(*drop.Type, drop.Field, thing, material, Assets);
    REQUIRE(History.Commit());

    const auto& component = thing.GetComponent<MeshRendererComponent>();
    REQUIRE(component.MaterialAssets.size() == 1);
    CHECK(component.MaterialAssets[0] == material);

    REQUIRE(History.Undo());
    CHECK(Capture() == before);

    // What the field no longer names is the default material again
    CHECK(component.MaterialAssets[0] != material);
    REQUIRE_FALSE(component.Resolved.SubMeshes.empty());
    CHECK(component.Resolved.Materials.size() == component.Resolved.SubMeshes.size());
    CHECK(component.Resolved.Materials[0] != Assets.Load(material));
}
