#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/asset_manager/meta_file.hpp>
#include <crimson/scene/scene_renderer.hpp>
#include <crimson/scene/scene_serializer.hpp>

#include "support/fake_gpu.hpp"
#include "support/scene_fields.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::FieldOf;
using crimson::tests::ResolveAssets;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";
    constexpr std::string_view kMaterial = R"({ "shader": "engine/shaders/S_Lit.hlsl" })";

    std::string ReadFile(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    std::string SceneWith(std::string_view entities)
    {
        return R"({ "version": 1, "entities": [ )" + std::string(entities) + " ] }";
    }

    std::string InstanceOf(std::string_view name, std::string_view path)
    {
        return R"({ "name": ")" + std::string(name) + R"(", "instance": ")" + std::string(path) + R"(" })";
    }

    struct Fixture
    {
        TempFolder Temp;
        FakeGpuResourceManager Gpu;

        void Open(AssetManager& assets, std::string_view path, Scene& scene) const
        {
            const SceneID id = assets.Find<AssetType::Scene>(path);
            const auto data = assets.Read(id);
            REQUIRE(data);
            InstantiateScene(*data, scene, {}, assets, id);
        }
    };
}

TEST_CASE_METHOD(Fixture, "A scene survives being saved and loaded", "[scene data]")
{
    Temp.Write("meshes/thing.obj", kTriangleObj);
    Temp.Write("materials/thing.mat", kMaterial);
    AssetManager assets(Gpu, Temp.Path());

    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    parent.GetComponent<NameComponent>().Active = false;

    auto& transform = parent.GetComponent<TransformComponent>();
    transform.Position = glm::vec3(1.5f, -2.25f, 3.1f);
    transform.RotationDegrees = glm::vec3(190.0f, -45.5f, 370.0f); // doesn't change into equal angles
    transform.Scale = glm::vec3(0.1f, 2.0f, 3.0f);

    const Entity light = scene.CreateEntity("Light", parent);
    light.AddComponent<LightComponent>(LightComponent{ .Type = LightType::Spot, .Color = glm::vec3(0.1f, 0.2f, 0.3f), .Intensity = 7.5f, .Range = 3.0f, .InnerConeDegrees = 5.0f, .OuterConeDegrees = 6.0f, .CastsShadow = true });

    const Entity mesh = scene.CreateEntity("Mesh");
    auto& renderer = mesh.AddComponent<MeshRendererComponent>();
    renderer.MeshAsset = assets.Find<AssetType::Mesh>("meshes/thing.obj");
    renderer.MaterialAssets = { assets.Find<AssetType::Material>("materials/thing.mat") };
    renderer.CastsShadows = false;
    ResolveAssets(assets, renderer);

    const SceneEnvironment environment{ .Ambient = glm::vec3(0.2f), .Exposure = 2.5f, .Mapper = ToneMapper::None };
    const SceneData saved = CaptureScene(scene, environment, assets);

    const auto parsed = ParseSceneData(WriteSceneData(saved, assets), assets, "test");
    REQUIRE(parsed);
    CHECK(*parsed == saved);

    Scene loaded;
    InstantiateScene(*parsed, loaded, {}, assets, {});
    CHECK(CaptureScene(loaded, environment, assets) == saved);

    REQUIRE(loaded.GetRoots().size() == 2);
    const Entity loadedMesh = loaded.GetEntity(loaded.GetRoots()[1]);
    CHECK(loadedMesh.GetComponent<NameComponent>().Id == mesh.GetComponent<NameComponent>().Id);
    CHECK_FALSE(loadedMesh.GetComponent<MeshRendererComponent>().Resolved.SubMeshes.empty());
}

TEST_CASE_METHOD(Fixture, "Numbers are written the short way", "[scene data]")
{
    AssetManager assets(Gpu, Temp.Path());

    SceneData data;
    data.Environment.Exposure = 0.1f;

    const std::string text = WriteSceneData(data, assets);
    CHECK(text.find("\"exposure\": 0.1,") != std::string::npos);
    CHECK(text.find("0.100000") == std::string::npos);
    CHECK(text.find("\"ambient\": [0.05, 0.05, 0.05]") != std::string::npos);
}

TEST_CASE_METHOD(Fixture, "Scene files written by hand work", "[scene data]")
{
    Temp.Write("thing.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseSceneData(SceneWith(R"({ "name": "A", "meshRenderer": { "mesh": "thing.obj" } })"), assets, "test");

    REQUIRE(data);
    REQUIRE(data->Roots.size() == 1);
    CHECK(data->Roots[0].Id);
    CHECK(data->Roots[0].Active);
    CHECK(FieldOf<glm::vec3>(assets, data->Roots[0], "transform", "scale") == glm::vec3(1.0f));
    CHECK(FieldOf<AssetID>(assets, data->Roots[0], "meshRenderer", "mesh") == assets.Find<AssetType::Mesh>("thing.obj"));
}

TEST_CASE_METHOD(Fixture, "A broken scene file doesn't load", "[scene data]")
{
    AssetManager assets(Gpu, Temp.Path());

    CHECK_FALSE(ParseSceneData("{ not json", assets, "test"));
    CHECK_FALSE(ParseSceneData(R"({ "version": 2 })", assets, "test"));
    CHECK_FALSE(ParseSceneData(R"({ "version": 0 })", assets, "test"));
    CHECK_FALSE(ParseSceneData(SceneWith(R"({ "position": "here" })"), assets, "test"));
    CHECK_FALSE(ParseSceneData(SceneWith(R"({ "id": "not hex" })"), assets, "test"));
    CHECK_FALSE(ParseSceneData(SceneWith(R"({ "light": 5 })"), assets, "test"));
    CHECK_FALSE(ParseSceneData(SceneWith(R"({ "light": { "intensity": "bright" } })"), assets, "test"));
}

TEST_CASE_METHOD(Fixture, "An empty material slot stays empty in the file", "[scene data]")
{
    Temp.Write("thing.obj", "mtllib thing.mtl\nusemtl Paint\nv 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n");
    Temp.Write("thing.mtl", "newmtl Paint\n");
    AssetManager assets(Gpu, Temp.Path());

    Scene scene;
    auto& renderer = scene.CreateEntity("Mesh").AddComponent<MeshRendererComponent>();
    renderer.MeshAsset = assets.Find<AssetType::Mesh>("thing.obj");
    ResolveAssets(assets, renderer);
    REQUIRE(renderer.MaterialAssets[0] == DefaultMaterial(assets));

    const SceneData data = CaptureScene(scene, {}, assets);
    CHECK_FALSE(FieldOf<std::vector<AssetID>>(assets, data.Roots[0], "meshRenderer", "materials")[0]);

    // So a material made for the slot afterwards gets used
    Temp.Write("materials/Paint.mat", kMaterial);
    assets.Refresh();

    Scene loaded;
    InstantiateScene(*ParseSceneData(WriteSceneData(data, assets), assets, "test"), loaded, {}, assets, {});

    const Entity entity = loaded.GetEntity(loaded.GetRoots()[0]);
    CHECK(entity.GetComponent<MeshRendererComponent>().MaterialAssets[0] == assets.Find<AssetType::Material>("materials/Paint.mat"));
}

TEST_CASE_METHOD(Fixture, "A slot on the default material finds one made for it, and a chosen one stays", "[scene data]")
{
    Temp.Write("thing.obj", "mtllib thing.mtl\nusemtl Paint\nv 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n");
    Temp.Write("thing.mtl", "newmtl Paint\n");
    Temp.Write("other.mat", kMaterial);
    AssetManager assets(Gpu, Temp.Path());

    Scene scene;
    auto& onDefault = scene.CreateEntity("Default").AddComponent<MeshRendererComponent>();
    onDefault.MeshAsset = assets.Find<AssetType::Mesh>("thing.obj");
    ResolveAssets(assets, onDefault);
    REQUIRE(onDefault.MaterialAssets[0] == DefaultMaterial(assets));

    auto& chosen = scene.CreateEntity("Chosen").AddComponent<MeshRendererComponent>();
    chosen.MeshAsset = assets.Find<AssetType::Mesh>("thing.obj");
    chosen.MaterialAssets = { assets.Find<AssetType::Material>("other.mat") };
    ResolveAssets(assets, chosen);

    // As the editor's Create Materials from .mtl does
    Temp.Write("materials/Paint.mat", kMaterial);
    assets.Refresh();

    ResolveAssets(assets, onDefault);
    ResolveAssets(assets, chosen);

    CHECK(onDefault.MaterialAssets[0] == assets.FindId("materials/Paint.mat"));
    CHECK(chosen.MaterialAssets[0] == assets.FindId("other.mat"));
}

TEST_CASE_METHOD(Fixture, "An instance's entities come from its scene", "[scene data]")
{
    Temp.Write("crate.scene", SceneWith(R"({ "name": "Lid", "position": [0, 1, 0], "children": [ { "name": "Knob" } ] }, { "name": "Base" })"));
    const fs::path level = Temp.Write("level.scene", SceneWith(R"({ "name": "Here", "position": [5, 0, 0], "instance": "crate.scene" })"));
    AssetManager assets(Gpu, Temp.Path());

    Scene scene;
    Open(assets, "level.scene", scene);

    REQUIRE(scene.GetRoots().size() == 1);
    const Entity root = scene.GetEntity(scene.GetRoots()[0]);

    SECTION("It holds the source's roots, which can't be changed")
    {
        CHECK(root.GetComponent<NameComponent>().Name == "Here");
        CHECK(root.GetComponent<SceneInstanceComponent>().Scene == assets.Find<AssetType::Scene>("crate.scene"));
        CHECK_FALSE(root.HasComponent<InstancedComponent>());

        const auto& children = root.GetComponent<RelationshipComponent>().Children;
        REQUIRE(children.size() == 2);

        const Entity lid = scene.GetEntity(children[0]);
        CHECK(lid.GetComponent<NameComponent>().Name == "Lid");
        CHECK(lid.GetComponent<TransformComponent>().Position == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(lid.HasComponent<InstancedComponent>());

        const Entity knob = scene.GetEntity(lid.GetComponent<RelationshipComponent>().Children[0]);
        CHECK(knob.HasComponent<InstancedComponent>());
    }

    SECTION("Saving only keeps the instance itself")
    {
        root.GetComponent<TransformComponent>().Position = glm::vec3(9.0f);

        const SceneData data = CaptureScene(scene, {}, assets);

        REQUIRE(data.Roots.size() == 1);
        CHECK(FieldOf<glm::vec3>(assets, data.Roots[0], "transform", "position") == glm::vec3(9.0f));
        CHECK(data.Roots[0].Instance == assets.Find<AssetType::Scene>("crate.scene"));
        CHECK(data.Roots[0].Children.empty());
    }

    SECTION("A changed source shows when it is opened again")
    {
        Temp.Write("crate.scene", SceneWith(R"({ "name": "Only" })"));

        Scene again;
        Open(assets, "level.scene", again);

        const auto& children = again.GetEntity(again.GetRoots()[0]).GetComponent<RelationshipComponent>().Children;
        REQUIRE(children.size() == 1);
        CHECK(again.GetEntity(children[0]).GetComponent<NameComponent>().Name == "Only");
    }

    SECTION("Emptying it leaves the entity with its reference")
    {
        EmptySceneInstance(root, scene);

        CHECK(root.GetComponent<RelationshipComponent>().Children.empty());
        CHECK(CaptureScene(scene, {}, assets).Roots[0].Instance == assets.FindId("crate.scene"));
    }
}

TEST_CASE_METHOD(Fixture, "A new instance is named after its file and placed under the parent", "[scene data]")
{
    Temp.Write("props/crate.scene", SceneWith(R"({ "name": "Lid" })"));
    AssetManager assets(Gpu, Temp.Path());

    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    const Entity instance = CreateSceneInstance(assets.Find<AssetType::Scene>("props/crate.scene"), scene, parent, assets, {});

    REQUIRE(instance);
    CHECK(instance.GetComponent<NameComponent>().Name == "crate");
    CHECK(instance.GetComponent<RelationshipComponent>().Parent == parent.GetHandle());
    CHECK(instance.GetComponent<RelationshipComponent>().Children.size() == 1);

    CHECK_FALSE(CreateSceneInstance(SceneID{}, scene, {}, assets, {}));
}

TEST_CASE_METHOD(Fixture, "A scene containing itself doesn't go on forever", "[scene data]")
{
    Temp.Write("self.scene", SceneWith(InstanceOf("Me", "self.scene")));
    Temp.Write("a.scene", SceneWith(InstanceOf("B", "b.scene")));
    Temp.Write("b.scene", SceneWith(InstanceOf("A", "a.scene")));
    AssetManager assets(Gpu, Temp.Path());

    SECTION("In itself")
    {
        Scene scene;
        Open(assets, "self.scene", scene);

        const Entity root = scene.GetEntity(scene.GetRoots()[0]);
        CHECK(root.HasComponent<SceneInstanceComponent>());
        CHECK(root.GetComponent<RelationshipComponent>().Children.empty());
    }

    SECTION("Through another scene")
    {
        Scene scene;
        Open(assets, "a.scene", scene);

        const Entity b = scene.GetEntity(scene.GetRoots()[0]);
        const auto& children = b.GetComponent<RelationshipComponent>().Children;
        REQUIRE(children.size() == 1);
        CHECK(scene.GetEntity(children[0]).GetComponent<RelationshipComponent>().Children.empty());
    }

    SECTION("It can't be dropped in to begin with")
    {
        Temp.Write("host.scene", SceneWith(""));
        Temp.Write("uses_host.scene", SceneWith(InstanceOf("Host", "host.scene")));
        assets.Refresh();

        const SceneID host = assets.Find<AssetType::Scene>("host.scene");
        const SceneID usesHost = assets.Find<AssetType::Scene>("uses_host.scene");

        CHECK(SceneUses(usesHost, host, assets));
        CHECK_FALSE(SceneUses(host, usesHost, assets));

        Scene scene;
        CHECK_FALSE(CreateSceneInstance(host, scene, {}, assets, host));
        CHECK_FALSE(CreateSceneInstance(usesHost, scene, {}, assets, host));
        CHECK(CreateSceneInstance(host, scene, {}, assets, usesHost));
        CHECK(scene.GetRoots().size() == 1);
    }
}

TEST_CASE_METHOD(Fixture, "An instance whose source can't be used stays an entity", "[scene data]")
{
    SECTION("A broken file keeps the reference")
    {
        Temp.Write("broken.scene", "{ not json");
        Temp.Write("level.scene", SceneWith(InstanceOf("Crate", "broken.scene")));
        AssetManager assets(Gpu, Temp.Path());

        Scene scene;
        Open(assets, "level.scene", scene);

        REQUIRE(scene.GetRoots().size() == 1);
        CHECK(CaptureScene(scene, {}, assets).Roots[0].Instance == assets.Find<AssetType::Scene>("broken.scene"));
    }

    SECTION("A missing file keeps the reference too")
    {
        Temp.Write("level.scene", SceneWith(InstanceOf("Crate", "gone.scene")));
        AssetManager assets(Gpu, Temp.Path());

        Scene scene;
        Open(assets, "level.scene", scene);

        REQUIRE(scene.GetRoots().size() == 1);
        const Entity root = scene.GetEntity(scene.GetRoots()[0]);
        CHECK(root.GetComponent<NameComponent>().Name == "Crate");
        REQUIRE(root.HasComponent<SceneInstanceComponent>());
        CHECK(assets.IsMissing(root.GetComponent<SceneInstanceComponent>().Scene));

        // Written by hand as a path, so it is written back that way
        const std::string text = WriteSceneData(CaptureScene(scene, {}, assets), assets);
        CHECK(text.find(R"("instance": "gone.scene")") != std::string::npos);
    }
}

TEST_CASE_METHOD(Fixture, "A scene keeps its references to missing files", "[scene data]")
{
    const fs::path meshFile = Temp.Write("thing.obj", kTriangleObj);
    const fs::path materialFile = Temp.Write("thing.mat", kMaterial);
    const fs::path crateFile = Temp.Write("crate.scene", SceneWith(""));
    const fs::path levelFile = Temp.Write("level.scene", SceneWith(
        R"({ "name": "Mesh", "meshRenderer": { "mesh": "thing.obj", "materials": [ "thing.mat" ] } }, )" + InstanceOf("Crate", "crate.scene")));

    // Saved once, for the references to have their guids
    {
        AssetManager assets(Gpu, Temp.Path());
        Scene scene;
        Open(assets, "level.scene", scene);
        REQUIRE(assets.Save(assets.Find<AssetType::Scene>("level.scene"), CaptureScene(scene, {}, assets)));
    }

    const std::string saved = ReadFile(levelFile);

    // Moved away with their meta files, as if they were missing for a while
    std::vector<fs::path> moved;
    for (const fs::path& file : { meshFile, materialFile, crateFile })
    {
        moved.push_back(file);
        moved.push_back(meta::MetaPathFor(file));
    }

    for (const fs::path& file : moved)
        fs::rename(file, fs::path(file) += ".away");

    {
        AssetManager assets(Gpu, Temp.Path());
        Scene scene;
        Open(assets, "level.scene", scene);

        const SceneData captured = CaptureScene(scene, {}, assets);
        REQUIRE(captured.Roots.size() == 2);
        CHECK(assets.GetMissing(FieldOf<AssetID>(assets, captured.Roots[0], "meshRenderer", "mesh"))->Path == "thing.obj");
        CHECK(assets.GetMissing(FieldOf<std::vector<AssetID>>(assets, captured.Roots[0], "meshRenderer", "materials").at(0))->Path == "thing.mat");
        CHECK(assets.GetMissing(captured.Roots[1].Instance)->Path == "crate.scene");

        REQUIRE(assets.Save(assets.Find<AssetType::Scene>("level.scene"), captured));
        CHECK(ReadFile(levelFile) == saved);
    }

    for (const fs::path& file : moved)
        fs::rename(fs::path(file) += ".away", file);

    {
        AssetManager assets(Gpu, Temp.Path());
        Scene scene;
        Open(assets, "level.scene", scene);

        const Entity mesh = scene.GetEntity(scene.GetRoots()[0]);
        const auto& renderer = mesh.GetComponent<MeshRendererComponent>();
        CHECK_FALSE(renderer.Resolved.SubMeshes.empty());
        CHECK(renderer.MaterialAssets.at(0) == assets.FindId("thing.mat"));
        CHECK(CaptureScene(scene, {}, assets).Roots[1].Instance == assets.FindId("crate.scene"));
    }
}

TEST_CASE_METHOD(Fixture, "A missing material stays in its slot, with the default drawn instead", "[scene data]")
{
    Temp.Write("thing.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseSceneData(SceneWith(R"({ "meshRenderer": { "mesh": "thing.obj", "materials": [ "gone.mat" ] } })"), assets, "test");
    REQUIRE(data);

    Scene scene;
    InstantiateScene(*data, scene, {}, assets, {});

    const auto& renderer = scene.GetEntity(scene.GetRoots()[0]).GetComponent<MeshRendererComponent>();
    CHECK(assets.IsMissing(renderer.MaterialAssets.at(0)));
    CHECK(renderer.Resolved.Materials.at(0) == assets.Load(DefaultMaterial(assets)));
    CHECK(CaptureScene(scene, {}, assets) == *data);
}

TEST_CASE_METHOD(Fixture, "Scenes are assets", "[scene data]")
{
    AssetManager assets(Gpu, Temp.Path());

    // The engine's template for new scenes is listed too
    const auto projectScenes = [&assets] { return std::ranges::count_if(assets.List(AssetType::Scene), [](const AssetInfo* scene) { return !scene->ReadOnly; }); };

    const SceneID id = assets.Create<AssetType::Scene>("scenes/new.scene");
    REQUIRE(id);
    CHECK(assets.GetInfo(id)->Type == AssetType::Scene);
    CHECK(projectScenes() == 1);
    CHECK(fs::exists(Temp.Path() / "scenes/new.scene.meta"));
    CHECK_FALSE(assets.Create<AssetType::Scene>("scenes/new.scene"));
    CHECK_FALSE(assets.Create<AssetType::Scene>("scenes/wrong.mat"));

    SECTION("A new one has a light")
    {
        const auto data = assets.Read(id);
        REQUIRE(data);
        REQUIRE(data->Roots.size() == 1);
        CHECK(FieldOf<int32_t>(assets, data->Roots[0], "light", "type") == static_cast<int32_t>(LightType::Directional));
        CHECK(data->Roots[0].Name == "Sun");

        const SceneID other = assets.Create<AssetType::Scene>("scenes/other.scene");
        REQUIRE(other);
        CHECK(assets.Read(other)->Roots[0].Id != data->Roots[0].Id);
    }

    SECTION("Saving replaces the file")
    {
        SceneData data;
        data.Environment.Exposure = 3.0f;
        REQUIRE(assets.Save(id, data));

        CHECK(assets.Read(id)->Environment.Exposure == 3.0f);
        CHECK_FALSE(fs::exists(Temp.Path() / "scenes/new.scene.saving"));
        CHECK(AssetManager(Gpu, Temp.Path()).Find<AssetType::Scene>("scenes/new.scene") == id);
    }

    SECTION("Deleting removes it")
    {
        REQUIRE(assets.Delete(id));
        CHECK_FALSE(assets.GetInfo(id));
        CHECK(projectScenes() == 0);
    }
}

TEST_CASE_METHOD(Fixture, "Moving a file updates the scenes referencing it", "[scene data]")
{
    Temp.Write("old.obj", kTriangleObj);
    Temp.Write("old.mat", kMaterial);
    Temp.Write("old.scene", SceneWith(""));

    const fs::path file = Temp.Write("level.scene", SceneWith(R"({
        "name": "Parent",
        "meshRenderer": { "mesh": "old.obj", "materials": [ "old.mat", null ] },
        "children": [ { "name": "Child", "instance": "old.scene" } ]
    })"));

    const std::string other = SceneWith(R"({ "name": "Unrelated", "scale": [ 1.0, 2.0, 3.0 ] })");
    const fs::path unrelated = Temp.Write("unrelated.scene", other);

    AssetManager assets(Gpu, Temp.Path());

    const MeshID mesh = assets.Find<AssetType::Mesh>("old.obj");
    const MaterialID material = assets.Find<AssetType::Material>("old.mat");
    const SceneID source = assets.Find<AssetType::Scene>("old.scene");

    REQUIRE(assets.Rename(mesh, "folder/new.obj"));
    REQUIRE(assets.Rename(material, "folder/new.mat"));
    REQUIRE(assets.Rename(source, "folder/new.scene"));

    CHECK(ReadFile(unrelated) == other);

    AssetManager reopened(Gpu, Temp.Path());
    const auto data = reopened.Read(reopened.Find<AssetType::Scene>("level.scene"));

    REQUIRE(data);
    CHECK(FieldOf<AssetID>(assets, data->Roots[0], "meshRenderer", "mesh") == mesh);
    CHECK(FieldOf<std::vector<AssetID>>(assets, data->Roots[0], "meshRenderer", "materials")[0] == material);
    CHECK_FALSE(FieldOf<std::vector<AssetID>>(assets, data->Roots[0], "meshRenderer", "materials")[1]);
    CHECK(data->Roots[0].Children[0].Instance == source);
}

TEST_CASE_METHOD(Fixture, "A renamed folder keeps a scene's references working", "[scene data]")
{
    Temp.Write("props/crate.obj", kTriangleObj);
    Temp.Write("props/crate.scene", SceneWith(R"({ "meshRenderer": { "mesh": "props/crate.obj" } })"));
    AssetManager assets(Gpu, Temp.Path());

    const MeshID mesh = assets.Find<AssetType::Mesh>("props/crate.obj");

    fs::rename(Temp.Path() / "props", Temp.Path() / "renamed");
    CHECK(assets.Refresh().empty());

    AssetManager reopened(Gpu, Temp.Path());
    const auto data = reopened.Read(reopened.Find<AssetType::Scene>("renamed/crate.scene"));

    REQUIRE(data);
    CHECK(FieldOf<AssetID>(assets, data->Roots[0], "meshRenderer", "mesh") == mesh);
}

TEST_CASE_METHOD(Fixture, "A component that isn't registered is kept as it was", "[scene data]")
{
    const std::string text = SceneWith(R"({ "name": "Door", "health": { "max": 3, "armor": [ 1, 2 ] }, "light": { "type": "spot" } })");
    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseSceneData(text, assets, "test");
    REQUIRE(data);

    const SceneComponent* health = data->Roots[0].FindComponent("health");
    REQUIRE(health);
    CHECK(health->Values.empty());
    CHECK_FALSE(health->Raw.empty());

    SECTION("Writing the file again keeps it")
    {
        const std::string written = WriteSceneData(*data, assets);
        CHECK(written.find(R"("max": 3)") != std::string::npos);

        const auto again = ParseSceneData(written, assets, "test");
        REQUIRE(again);
        CHECK(*again == *data);
    }

    SECTION("Loading the scene and saving it again keeps it")
    {
        Scene scene;
        InstantiateScene(*data, scene, {}, assets, {});

        const SceneData captured = CaptureScene(scene, {}, assets);
        REQUIRE(captured.Roots.size() == 1);
        REQUIRE(captured.Roots[0].FindComponent("health"));
        CHECK(*captured.Roots[0].FindComponent("health") == *health);
        CHECK(captured.Roots[0].FindComponent("light"));
    }
}

TEST_CASE_METHOD(Fixture, "A component registered at runtime is saved and loaded with its values", "[scene data]")
{
    Temp.Write("sword.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());
    const MeshID sword = assets.Find<AssetType::Mesh>("sword.obj");

    const ComponentType* health = assets.Components().RegisterRuntime("health", "Health", {
        FieldInfo{ .Key = "max", .Label = "Max", .Type = FieldType::Int, .Default = int32_t{ 3 } },
        FieldInfo{ .Key = "weapon", .Label = "Weapon", .Type = FieldType::Asset, .Default = AssetID{}, .Asset = AssetType::Mesh }
    });
    REQUIRE(health);

    Scene scene;
    const Entity guard = scene.CreateEntity("Guard");
    health->Add(guard);
    WriteComponent(*health, guard, { int32_t{ 10 }, AssetID(sword) });

    const std::string text = WriteSceneData(CaptureScene(scene, {}, assets), assets);
    CHECK(text.find(R"("health")") != std::string::npos);

    const auto data = ParseSceneData(text, assets, "test");
    REQUIRE(data);
    CHECK(FieldOf<int32_t>(assets, data->Roots[0], "health", "max") == 10);
    CHECK(FieldOf<AssetID>(assets, data->Roots[0], "health", "weapon") == sword);

    Scene loaded;
    InstantiateScene(*data, loaded, {}, assets, {});

    const Entity loadedGuard = loaded.GetEntity(loaded.GetRoots()[0]);
    REQUIRE(health->Has(loadedGuard));
    CHECK(std::get<int32_t>(ReadComponent(*health, loadedGuard)[0]) == 10);
    CHECK(std::get<AssetID>(ReadComponent(*health, loadedGuard)[1]) == sword);

    SECTION("Where it isn't registered, the file keeps it as it was")
    {
        AssetManager other(Gpu, Temp.Path());

        const auto unregistered = ParseSceneData(text, other, "test");
        REQUIRE(unregistered);
        CHECK_FALSE(unregistered->Roots[0].FindComponent("health")->Raw.empty());
        CHECK(WriteSceneData(*unregistered, other) == text);
    }
}

TEST_CASE_METHOD(Fixture, "A light's fields are read from a file as they were written before", "[scene data]")
{
    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseSceneData(SceneWith(R"({ "light": { "type": "spot", "color": [ 1.0, 0.5, 0.2 ], "intensity": 10.0, "range": 12.0,
        "innerCone": 20.0, "outerCone": 35.0, "castsShadow": true } })"), assets, "test");
    REQUIRE(data);

    Scene scene;
    InstantiateScene(*data, scene, {}, assets, {});

    const LightComponent& light = scene.GetEntity(scene.GetRoots()[0]).GetComponent<LightComponent>();
    CHECK(light == LightComponent{ .Type = LightType::Spot, .Color = glm::vec3(1.0f, 0.5f, 0.2f), .Intensity = 10.0f, .Range = 12.0f,
                                   .InnerConeDegrees = 20.0f, .OuterConeDegrees = 35.0f, .CastsShadow = true });

    SECTION("A type it doesn't know is a point light, with a warning")
    {
        const auto unknown = ParseSceneData(SceneWith(R"({ "light": { "type": "laser" } })"), assets, "test");
        REQUIRE(unknown);
        CHECK(FieldOf<int32_t>(assets, unknown->Roots[0], "light", "type") == static_cast<int32_t>(LightType::Point));
    }
}

TEST_CASE_METHOD(Fixture, "A transform's keys stay at the node's level", "[scene data]")
{
    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseSceneData(SceneWith(R"({ "name": "A", "position": [1, 2, 3], "scale": [2, 2, 2] })"), assets, "test");
    REQUIRE(data);
    REQUIRE(data->Roots.size() == 1);

    CHECK(FieldOf<glm::vec3>(assets, data->Roots[0], "transform", "position") == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(FieldOf<glm::vec3>(assets, data->Roots[0], "transform", "rotation") == glm::vec3(0.0f));
    CHECK(FieldOf<glm::vec3>(assets, data->Roots[0], "transform", "scale") == glm::vec3(2.0f));

    const std::string text = WriteSceneData(*data, assets);
    CHECK(text.find("\"position\": [1.0, 2.0, 3.0]") != std::string::npos);
    CHECK(text.find("\"transform\"") == std::string::npos);

    CHECK(ParseSceneData(text, assets, "test") == *data);
}

TEST_CASE_METHOD(Fixture, "Every kind of field survives saving and loading", "[scene data]")
{
    Temp.Write("meshes/thing.obj", kTriangleObj);
    Temp.Write("materials/thing.mat", kMaterial);
    AssetManager assets(Gpu, Temp.Path());

    const ComponentType* everything = assets.Components().RegisterRuntime("everything", "Everything", {
        FieldInfo{ .Key = "flag", .Label = "Flag", .Type = FieldType::Bool, .Default = false },
        FieldInfo{ .Key = "count", .Label = "Count", .Type = FieldType::Int, .Default = int32_t{ 0 } },
        FieldInfo{ .Key = "amount", .Label = "Amount", .Type = FieldType::Float, .Default = 0.0f },
        FieldInfo{ .Key = "offset", .Label = "Offset", .Type = FieldType::Vec3, .Default = glm::vec3(0.0f) },
        FieldInfo{ .Key = "tint", .Label = "Tint", .Type = FieldType::Color, .Default = glm::vec3(1.0f) },
        FieldInfo{ .Key = "title", .Label = "Title", .Type = FieldType::String, .Default = std::string() },
        FieldInfo{ .Key = "kind", .Label = "Kind", .Type = FieldType::Enum, .Default = int32_t{ 0 }, .EnumOptions = { { "first", "First" }, { "second", "Second" } } },
        FieldInfo{ .Key = "mesh", .Label = "Mesh", .Type = FieldType::Asset, .Default = AssetID{}, .Asset = AssetType::Mesh },
        FieldInfo{ .Key = "materials", .Label = "Materials", .Type = FieldType::AssetList, .Default = std::vector<AssetID>(), .Asset = AssetType::Material }
    });
    REQUIRE(everything);

    const ComponentRegistry& components = assets.Components();

    SceneNode node{ .Id = AssetID::Generate(), .Name = "Thing" };
    node.Components.push_back(components.Compose("transform", { { "position", glm::vec3(1.0f, 2.0f, 3.0f) } }));
    node.Components.push_back(components.Compose("everything", {
        { "flag", true },
        { "count", int32_t{ 7 } },
        { "amount", 1.25f },
        { "offset", glm::vec3(1.0f, -2.0f, 3.5f) },
        { "tint", glm::vec3(0.5f, 0.25f, 0.125f) },
        { "title", std::string("hello") },
        { "kind", int32_t{ 1 } },
        { "mesh", AssetID(assets.Find<AssetType::Mesh>("meshes/thing.obj")) },
        { "materials", std::vector<AssetID>{ assets.Find<AssetType::Material>("materials/thing.mat"), AssetID{} } } }));

    SceneData data;
    data.Roots.push_back(node);

    const std::string text = WriteSceneData(data, assets);
    CHECK(text.find("\"kind\": \"second\"") != std::string::npos);

    const auto parsed = ParseSceneData(text, assets, "test");
    REQUIRE(parsed);
    CHECK(*parsed == data);
}

TEST_CASE_METHOD(Fixture, "A file saved before reads and writes back the same", "[scene data]")
{
    std::ifstream stream(std::string(CRIMSON_SOURCE_DIR) + "/samples/Demo/assets/scenes/demo.scene", std::ios::binary);
    REQUIRE(stream);

    // Git may have checked it out with CRLF
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::erase(text, '\r');

    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseSceneData(text, assets, "demo.scene");
    REQUIRE(data);
    CHECK_FALSE(data->Roots.empty());
    CHECK(WriteSceneData(*data, assets) == text);
}

TEST_CASE_METHOD(Fixture, "Moving a file updates a runtime component's asset field", "[scene data]")
{
    Temp.Write("meshes/thing.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());

    REQUIRE(assets.Components().RegisterRuntime("holder", "Holder", {
        FieldInfo{ .Key = "mesh", .Label = "Mesh", .Type = FieldType::Asset, .Default = AssetID{}, .Asset = AssetType::Mesh },
        FieldInfo{ .Key = "title", .Label = "Title", .Type = FieldType::String, .Default = std::string() }
    }));

    const std::string text = SceneWith(R"({ "name": "A", "holder": { "mesh": "old/thing.obj", "title": "old/thing.obj" } })");
    const std::unordered_map<std::string, AssetID> moved = { { "old/thing.obj", assets.Find<AssetType::Mesh>("meshes/thing.obj") } };

    const auto updated = UpdateSceneReferences(text, moved, assets);
    REQUIRE(updated);
    CHECK(updated->find("\"path\": \"meshes/thing.obj\"") != std::string::npos);

    // A string field that only looks like a reference is left alone
    CHECK(updated->find("\"title\": \"old/thing.obj\"") != std::string::npos);

    CHECK_FALSE(UpdateSceneReferences(*updated, moved, assets));
}
