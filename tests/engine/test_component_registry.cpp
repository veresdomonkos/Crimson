#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/scene/component_builder.hpp>
#include <crimson/scene/component_registry.hpp>

#include "support/fake_gpu.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;

namespace
{
    // Apart from the fixture, so the files are there before the asset manager looks
    struct ProjectFiles
    {
        TempFolder Temp;

        ProjectFiles()
        {
            Temp.Write("thing.obj", "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n");
            Temp.Write("thing.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl" })");
            Temp.Write("two.obj", "mtllib two.mtl\nv 0 0 0\nv 1 0 0\nv 0 2 0\nv 0 0 1\nusemtl stone\nf 1 2 3\nusemtl wood\nf 1 2 4\n");
            Temp.Write("two.mtl", "newmtl stone\nnewmtl wood\n");
        }
    };

    struct Fixture : ProjectFiles
    {
        FakeGpuResourceManager Gpu;
        AssetManager Assets{ Gpu, Temp.Path() };
        ComponentRegistry& Registry = Assets.Components();
        Scene World;
        Entity Thing = World.CreateEntity("Thing");

        const ComponentType& Type(std::string_view id) const
        {
            const ComponentType* type = Registry.Find(id);
            REQUIRE(type);
            return *type;
        }

        static const FieldInfo& Field(const ComponentType& type, std::string_view key)
        {
            const std::optional<size_t> index = type.FindField(key);
            REQUIRE(index);
            return type.Fields[*index];
        }
    };

    // One member of every kind the builder has a method for
    struct Probe
    {
        bool Flag = true;
        int32_t Count = 4;
        float Amount = 2.5f;
        glm::vec3 Offset{ 1.0f, 2.0f, 3.0f };
        std::string Title = "probe";
        glm::vec3 Tint{ 0.5f };
        LightType Kind = LightType::Spot;
        MeshID Mesh;
        std::vector<MaterialID> Materials;
    };

    std::vector<FieldInfo> HealthFields()
    {
        return {
            FieldInfo{ .Key = "max", .Label = "Max", .Type = FieldType::Int, .Default = int32_t{ 3 } },
            FieldInfo{ .Key = "regen", .Label = "Regen", .Type = FieldType::Float, .Default = 0.5f },
            FieldInfo{ .Key = "tag", .Label = "Tag", .Type = FieldType::String, .Default = 1.0f } // of the wrong type
        };
    }
}

TEST_CASE_METHOD(Fixture, "The engine's components are registered in the order they are saved in", "[components]")
{
    REQUIRE(Registry.Types().size() >= 3);
    CHECK(Registry.Types()[0].Id == "transform");
    CHECK(Registry.Types()[1].Id == "light");
    CHECK(Registry.Types()[2].Id == "meshRenderer");

    CHECK(Type("transform").InlineInFile);
    CHECK_FALSE(Type("transform").Addable);
    CHECK(Type("light").Addable);
    CHECK_FALSE(Registry.Find("nothing"));
}

TEST_CASE_METHOD(Fixture, "A native component's fields go through its members", "[components]")
{
    const ComponentType& light = Type("light");

    SECTION("Their defaults are the type's own")
    {
        CHECK(std::get<float>(Field(light, "intensity").Default) == LightComponent{}.Intensity);
        CHECK(std::get<int32_t>(Field(light, "type").Default) == static_cast<int32_t>(LightType::Point));
    }

    SECTION("Reading and writing them changes the entity's component")
    {
        Thing.AddComponent<LightComponent>(LightComponent{ .Type = LightType::Spot, .Intensity = 4.0f });

        std::vector<FieldValue> values = ReadComponent(light, Thing);
        REQUIRE(values.size() == light.Fields.size());
        CHECK(std::get<int32_t>(values[*light.FindField("type")]) == static_cast<int32_t>(LightType::Spot));
        CHECK(std::get<float>(values[*light.FindField("intensity")]) == 4.0f);

        values[*light.FindField("intensity")] = 7.0f;
        values[*light.FindField("type")] = static_cast<int32_t>(LightType::Directional);
        WriteComponent(light, Thing, values);

        CHECK(Thing.GetComponent<LightComponent>().Intensity == 7.0f);
        CHECK(Thing.GetComponent<LightComponent>().Type == LightType::Directional);
    }

    SECTION("An enum's keys are in the order of its values")
    {
        const FieldInfo& type = Field(light, "type");
        REQUIRE(type.EnumOptions.size() == 3);
        CHECK(type.EnumOptions[static_cast<size_t>(LightType::Spot)].Key == "spot");
        CHECK(type.EnumOptions[static_cast<size_t>(LightType::Directional)].Label == "Directional");
    }

    SECTION("Some only mean something with the others' values")
    {
        const LightComponent directional{ .Type = LightType::Directional };
        const LightComponent spot{ .Type = LightType::Spot };

        CHECK_FALSE(Field(light, "range").Visible(&directional));
        CHECK(Field(light, "range").Visible(&spot));
        CHECK(Field(light, "innerCone").Visible(&spot));
        CHECK_FALSE(Field(light, "intensity").Visible);
    }

    SECTION("Writing keeps them consistent")
    {
        const LightComponent defaults;
        std::vector<FieldValue> values = ReadFields(light, &defaults);
        values[*light.FindField("outerCone")] = 30.0f;
        values[*light.FindField("innerCone")] = 50.0f;

        WriteComponent(light, Thing, values);

        CHECK(Thing.GetComponent<LightComponent>().InnerConeDegrees == 30.0f);
    }

    SECTION("A value of another type leaves the field as it was")
    {
        const LightComponent defaults;
        std::vector<FieldValue> values = ReadFields(light, &defaults);
        values[*light.FindField("intensity")] = std::string("bright");

        WriteComponent(light, Thing, values);

        CHECK(Thing.GetComponent<LightComponent>().Intensity == LightComponent{}.Intensity);
    }
}

TEST_CASE_METHOD(Fixture, "Asset fields keep their asset's type", "[components]")
{
    const ComponentType& renderer = Type("meshRenderer");
    const MeshID mesh = Assets.Find<AssetType::Mesh>("thing.obj");
    const MaterialID material = Assets.Find<AssetType::Material>("thing.mat");
    REQUIRE(mesh);
    REQUIRE(material);

    CHECK(Field(renderer, "mesh").Asset == AssetType::Mesh);
    CHECK(Field(renderer, "materials").Asset == AssetType::Material);

    const MeshRendererComponent defaults;
    std::vector<FieldValue> values = ReadFields(renderer, &defaults);
    values[*renderer.FindField("mesh")] = AssetID(mesh);
    values[*renderer.FindField("materials")] = std::vector<AssetID>{ material, mesh };
    WriteComponent(renderer, Thing, values);

    const auto& component = Thing.GetComponent<MeshRendererComponent>();
    CHECK(component.MeshAsset == mesh);

    // A mesh isn't a material, so that slot is left empty
    REQUIRE(component.MaterialAssets.size() == 2);
    CHECK(component.MaterialAssets[0] == material);
    CHECK_FALSE(component.MaterialAssets[1]);
}

TEST_CASE_METHOD(Fixture, "A component is added, found and removed through its type", "[components]")
{
    const ComponentType& light = Type("light");

    CHECK_FALSE(light.Has(Thing));
    CHECK_FALSE(light.Get(Thing));

    light.Add(Thing);
    CHECK(light.Has(Thing));
    CHECK(light.Get(Thing) == Thing.TryGetComponent<LightComponent>());

    light.Remove(Thing);
    CHECK_FALSE(Thing.HasComponent<LightComponent>());
}

TEST_CASE_METHOD(Fixture, "A component can be registered from data, with no C++ type", "[components]")
{
    const ComponentType* health = Registry.RegisterRuntime("health", "Health", HealthFields());
    REQUIRE(health);
    CHECK(Registry.Find("health") == health);

    SECTION("A default of the wrong type becomes the empty one")
    {
        CHECK(std::get<std::string>(health->Fields[2].Default).empty());
    }

    SECTION("It is added with its defaults, edited and removed")
    {
        CHECK_FALSE(health->Has(Thing));
        health->Add(Thing);
        REQUIRE(health->Has(Thing));

        std::vector<FieldValue> values = ReadComponent(*health, Thing);
        REQUIRE(values.size() == 3);
        CHECK(std::get<int32_t>(values[0]) == 3);
        CHECK(std::get<float>(values[1]) == 0.5f);

        values[0] = int32_t{ 10 };
        values[2] = std::string("boss");
        WriteComponent(*health, Thing, values);

        CHECK(std::get<int32_t>(ReadComponent(*health, Thing)[0]) == 10);
        CHECK(std::get<std::string>(ReadComponent(*health, Thing)[2]) == "boss");

        health->Remove(Thing);
        CHECK_FALSE(health->Has(Thing));
    }

    SECTION("Each entity has its own values, which go with it")
    {
        const Entity other = World.CreateEntity("Other");
        health->Add(Thing);
        health->Add(other);

        WriteComponent(*health, other, { int32_t{ 7 } });
        CHECK(std::get<int32_t>(ReadComponent(*health, Thing)[0]) == 3);

        World.DestroyEntity(other);
        CHECK(health->Has(Thing));
    }

    SECTION("Its id can't be taken twice")
    {
        const LogCounter errors(LogLevel::Error, "health");

        CHECK_FALSE(Registry.RegisterRuntime("health", "Another", {}));
        CHECK(errors.Count() == 1);
        CHECK(Registry.Find("health")->Name == "Health");
    }
}

TEST_CASE_METHOD(Fixture, "A component is copied from one entity to another", "[components]")
{
    const Entity other = World.CreateEntity("Other");
    Thing.AddComponent<LightComponent>(LightComponent{ .Color = glm::vec3(1.0f, 0.0f, 0.0f), .Intensity = 2.5f });

    CopyComponent(Type("light"), Thing, other);

    REQUIRE(other.HasComponent<LightComponent>());
    CHECK(other.GetComponent<LightComponent>() == Thing.GetComponent<LightComponent>());

    SECTION("Not when the first one doesn't have it")
    {
        CopyComponent(Type("meshRenderer"), Thing, other);
        CHECK_FALSE(other.HasComponent<MeshRendererComponent>());
    }
}

TEST_CASE_METHOD(Fixture, "A builder's field takes its type and default from the member", "[components]")
{
    Registry.Register<Probe>("probe", "Probe")
        .Field("flag", "Flag", &Probe::Flag)
        .Field("count", "Count", &Probe::Count)
        .Field("amount", "Amount", &Probe::Amount)
        .Field("offset", "Offset", &Probe::Offset)
        .Field("title", "Title", &Probe::Title)
        .Color("tint", "Tint", &Probe::Tint)
        .Enum("kind", "Kind", &Probe::Kind, { { "directional", "Directional" }, { "point", "Point" }, { "spot", "Spot" } })
        .Asset("mesh", "Mesh", &Probe::Mesh)
        .AssetList("materials", "Materials", &Probe::Materials);

    const ComponentType& probe = Type("probe");
    REQUIRE(probe.Fields.size() == 9);

    CHECK(Field(probe, "flag").Type == FieldType::Bool);
    CHECK(std::get<bool>(Field(probe, "flag").Default));
    CHECK(Field(probe, "count").Type == FieldType::Int);
    CHECK(std::get<int32_t>(Field(probe, "count").Default) == 4);
    CHECK(Field(probe, "amount").Type == FieldType::Float);
    CHECK(std::get<float>(Field(probe, "amount").Default) == 2.5f);
    CHECK(Field(probe, "offset").Type == FieldType::Vec3);
    CHECK(std::get<glm::vec3>(Field(probe, "offset").Default) == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(Field(probe, "title").Type == FieldType::String);
    CHECK(std::get<std::string>(Field(probe, "title").Default) == "probe");
    CHECK(Field(probe, "tint").Type == FieldType::Color);
    CHECK(std::get<glm::vec3>(Field(probe, "tint").Default) == glm::vec3(0.5f));

    CHECK(Field(probe, "kind").Type == FieldType::Enum);
    CHECK(std::get<int32_t>(Field(probe, "kind").Default) == static_cast<int32_t>(LightType::Spot));
    CHECK(Field(probe, "kind").EnumOptions.size() == 3);

    CHECK(Field(probe, "mesh").Type == FieldType::Asset);
    CHECK(Field(probe, "mesh").Asset == AssetType::Mesh);
    CHECK(std::get<AssetID>(Field(probe, "mesh").Default) == AssetID{});
    CHECK(Field(probe, "materials").Type == FieldType::AssetList);
    CHECK(Field(probe, "materials").Asset == AssetType::Material);
    CHECK(std::get<std::vector<AssetID>>(Field(probe, "materials").Default).empty());

    SECTION("Writing sets the member, and an asset keeps its type")
    {
        const Probe defaults;
        std::vector<FieldValue> values = ReadFields(probe, &defaults);
        values[*probe.FindField("count")] = int32_t{ 9 };
        values[*probe.FindField("kind")] = static_cast<int32_t>(LightType::Point);
        values[*probe.FindField("mesh")] = AssetID(Assets.Find<AssetType::Mesh>("thing.obj"));
        WriteComponent(probe, Thing, values);

        const Probe& written = *static_cast<const Probe*>(probe.Get(Thing));
        CHECK(written.Count == 9);
        CHECK(written.Kind == LightType::Point);
        CHECK(written.Mesh == Assets.Find<AssetType::Mesh>("thing.obj"));
    }
}

TEST_CASE_METHOD(Fixture, "Native types are found by their C++ type", "[components]")
{
    CHECK(Registry.Find<LightComponent>() == &Type("light"));
    CHECK(Registry.Find<MeshRendererComponent>() == &Type("meshRenderer"));
    CHECK_FALSE(Registry.Find<Probe>());

    const ComponentType* health = Registry.RegisterRuntime("health", "Health", HealthFields());
    REQUIRE(health);
    CHECK(health->NativeType == 0);
    CHECK(Type("light").NativeType != 0);
}

TEST_CASE_METHOD(Fixture, "Each engine component names its icon", "[components]")
{
    const LightComponent directional{ .Type = LightType::Directional };
    const LightComponent point{ .Type = LightType::Point };
    const LightComponent spot{ .Type = LightType::Spot };
    const MeshRendererComponent renderer;
    const TransformComponent transform;

    CHECK(Type("transform").Icon(&transform) == "move-3d");
    CHECK(Type("meshRenderer").Icon(&renderer) == "box");
    CHECK(Type("light").Icon(&directional) == "sun");
    CHECK(Type("light").Icon(&point) == "lightbulb");
    CHECK(Type("light").Icon(&spot) == "cone");
}

TEST_CASE_METHOD(Fixture, "Presets set up a component and resolve its assets", "[components]")
{
    const auto preset = [](const ComponentType& type, std::string_view name) -> const ComponentPreset&
    {
        const auto found = std::ranges::find(type.Presets, name, &ComponentPreset::Name);
        REQUIRE(found != type.Presets.end());
        return *found;
    };

    SECTION("A light's type")
    {
        const ComponentType& light = Type("light");
        CHECK(preset(light, "Directional").Menu == "Light");

        ApplyPreset(light, preset(light, "Directional"), Thing, Assets);
        CHECK(Thing.GetComponent<LightComponent>().Type == LightType::Directional);
    }

    SECTION("A built-in mesh")
    {
        const ComponentType& renderer = Type("meshRenderer");
        CHECK(preset(renderer, "Cube").Menu == "3D Object");

        ApplyPreset(renderer, preset(renderer, "Cube"), Thing, Assets);

        const auto& component = Thing.GetComponent<MeshRendererComponent>();
        CHECK(component.MeshAsset == Assets.Find<AssetType::Mesh>("engine/meshes/SM_Cube.obj"));
        CHECK_FALSE(component.Resolved.SubMeshes.empty());
    }
}

TEST_CASE_METHOD(Fixture, "A slot's label comes from the mesh", "[components]")
{
    const ComponentType& renderer = Type("meshRenderer");
    const FieldInfo& materials = Field(renderer, "materials");
    REQUIRE(materials.ItemLabel);

    CreateComponent(renderer, Thing, Assets);
    AssignAsset(renderer, *renderer.FindField("mesh"), Thing, Assets.Find<AssetType::Mesh>("two.obj"), Assets);

    const void* component = renderer.Get(Thing);
    CHECK(materials.ItemLabel(component, 0) == "stone");
    CHECK(materials.ItemLabel(component, 1) == "wood");
    CHECK(materials.ItemLabel(component, 2) == "Material 2");
}

TEST_CASE_METHOD(Fixture, "An asset fallback is saved as empty", "[components]")
{
    const ComponentType& renderer = Type("meshRenderer");
    const MaterialID chosen = Assets.Find<AssetType::Material>("thing.mat");
    const size_t materials = *renderer.FindField("materials");

    CreateComponent(renderer, Thing, Assets);
    AssignAsset(renderer, *renderer.FindField("mesh"), Thing, Assets.Find<AssetType::Mesh>("two.obj"), Assets);
    AssignAsset(renderer, materials, Thing, chosen, Assets);

    const auto& component = Thing.GetComponent<MeshRendererComponent>();
    REQUIRE(component.MaterialAssets.size() == 2);
    CHECK(component.MaterialAssets[1] == DefaultMaterial(Assets));

    const SceneComponent captured = CaptureComponent(renderer, Thing, Assets);
    const auto& saved = std::get<std::vector<AssetID>>(captured.Values[materials]);

    REQUIRE(saved.size() == 2);
    CHECK(saved[0] == AssetID(chosen));
    CHECK_FALSE(saved[1]);

    // Only what is saved is changed
    CHECK(component.MaterialAssets[1] == DefaultMaterial(Assets));
}

TEST_CASE_METHOD(Fixture, "Dropped assets find their field", "[components]")
{
    const ComponentType& renderer = Type("meshRenderer");

    const FieldRef spawn = FindSpawnField(Registry, AssetType::Mesh);
    REQUIRE(spawn);
    CHECK(spawn.Type == &renderer);
    CHECK(renderer.Fields[spawn.Field].Key == "mesh");
    CHECK_FALSE(FindSpawnField(Registry, AssetType::Material));
    CHECK_FALSE(FindSpawnField(Registry, AssetType::Texture));

    SECTION("An entity takes what it has a field for")
    {
        CHECK_FALSE(FindDropField(Registry, Thing, AssetType::Material));

        CreateComponent(renderer, Thing, Assets);

        const FieldRef drop = FindDropField(Registry, Thing, AssetType::Material);
        REQUIRE(drop);
        CHECK(drop.Type == &renderer);
        CHECK(renderer.Fields[drop.Field].Key == "materials");

        // The mesh makes entities, it isn't set on one
        CHECK_FALSE(FindDropField(Registry, Thing, AssetType::Mesh));
    }

    SECTION("Assigning sets the first slot and resolves")
    {
        CreateComponent(renderer, Thing, Assets);
        AssignAsset(renderer, spawn.Field, Thing, Assets.Find<AssetType::Mesh>("two.obj"), Assets);

        const auto& component = Thing.GetComponent<MeshRendererComponent>();
        CHECK(component.MeshAsset == Assets.Find<AssetType::Mesh>("two.obj"));
        REQUIRE(component.MaterialAssets.size() == 2);

        const FieldRef drop = FindDropField(Registry, Thing, AssetType::Material);
        const MaterialID material = Assets.Find<AssetType::Material>("thing.mat");
        AssignAsset(*drop.Type, drop.Field, Thing, material, Assets);

        CHECK(component.MaterialAssets[0] == material);
        CHECK(component.Resolved.Materials[0] == Assets.Load(material));
    }

    SECTION("A list without slots has nowhere to put it")
    {
        CreateComponent(renderer, Thing, Assets);

        const FieldRef drop = FindDropField(Registry, Thing, AssetType::Material);
        AssignAsset(*drop.Type, drop.Field, Thing, Assets.Find<AssetType::Material>("thing.mat"), Assets);

        CHECK(Thing.GetComponent<MeshRendererComponent>().MaterialAssets.empty());
    }
}

TEST_CASE_METHOD(Fixture, "Composing a scene component starts from the defaults", "[components]")
{
    const ComponentType& light = Type("light");
    const LightComponent defaults;

    SECTION("Overrides replace the defaults by key")
    {
        const SceneComponent composed = Registry.Compose("light", { { "intensity", 3.0f }, { "castsShadow", true } });

        CHECK(composed.Type == "light");
        REQUIRE(composed.Values.size() == light.Fields.size());
        CHECK(*Registry.FieldOf(composed, "intensity") == FieldValue(3.0f));
        CHECK(*Registry.FieldOf(composed, "castsShadow") == FieldValue(true));
        CHECK(*Registry.FieldOf(composed, "range") == FieldValue(defaults.Range));
    }

    SECTION("A key or a type that isn't there is an error")
    {
        const LogCounter errors(LogLevel::Error, "[Components]");

        const SceneComponent composed = Registry.Compose("light", { { "luminosity", 3.0f } });
        CHECK(errors.Count() == 1);
        CHECK(*Registry.FieldOf(composed, "intensity") == FieldValue(defaults.Intensity));

        CHECK(Registry.Compose("nothing", {}).Values.empty());
        CHECK(errors.Count() == 2);
    }
}

TEST_CASE_METHOD(Fixture, "An id that clashes with a node key is refused", "[components]")
{
    const LogCounter errors(LogLevel::Error, "can't be a component's id");

    CHECK_FALSE(Registry.RegisterRuntime("position", "Position", {}));
    CHECK(errors.Count() == 1);

    CHECK_FALSE(Registry.RegisterRuntime("children", "Children", {}));
    CHECK(errors.Count() == 2);

    CHECK(Registry.IsNodeKey("scale"));
    CHECK_FALSE(Registry.IsNodeKey("light"));
}

TEST_CASE_METHOD(Fixture, "Every component with assets resolves with the scene", "[components]")
{
    const MeshID mesh = Assets.Find<AssetType::Mesh>("thing.obj");

    const Entity other = World.CreateEntity("Other");
    for (const Entity entity : { Thing, other })
    {
        entity.AddComponent<MeshRendererComponent>().MeshAsset = mesh;
        CHECK(entity.GetComponent<MeshRendererComponent>().Resolved.SubMeshes.empty());
    }

    ResolveSceneAssets(World, Assets);

    CHECK_FALSE(Thing.GetComponent<MeshRendererComponent>().Resolved.SubMeshes.empty());
    CHECK_FALSE(other.GetComponent<MeshRendererComponent>().Resolved.SubMeshes.empty());
}
