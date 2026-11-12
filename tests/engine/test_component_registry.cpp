#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
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
        REQUIRE(type.EnumKeys.size() == 3);
        CHECK(type.EnumKeys[static_cast<size_t>(LightType::Spot)] == "spot");
        CHECK(type.EnumLabels[static_cast<size_t>(LightType::Directional)] == "Directional");
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
