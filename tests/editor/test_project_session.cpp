#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>
#include <crimson/scene/scene_camera.hpp>
#include <crimson/scene/scene_serializer.hpp>
#include <crimson_editor/panels/asset_field.hpp>
#include <crimson_editor/project_session.hpp>
#include <crimson_editor/recent_projects.hpp>

#include "support/fake_gpu.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::AssetFieldText;
using crimson::editor::ProjectSession;
using crimson::editor::RecentProjects;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kQuad = "v -1 0 1\nv 1 0 1\nv 1 0 -1\nv -1 0 -1\nvn 0 1 0\nf 1//1 2//1 3//1 4//1\n";

    // With ids, as the editor saves them. A file without any gets them written in when it is first read, so they stay.
    constexpr std::string_view kMainScene = R"({ "version": 1, "entities": [
            { "id": "00000000-0000-4000-8000-0000000000a1", "name": "Floor", "meshRenderer": { "mesh": "meshes/floor.obj", "materials": [ "materials/floor.mat" ] } },
            { "id": "00000000-0000-4000-8000-0000000000a2", "name": "Pyramid", "meshRenderer": { "mesh": "meshes/pyramid.obj" } },
            { "id": "00000000-0000-4000-8000-0000000000a3", "name": "Sun", "light": { "type": "directional" } }
        ] })";

    // One pixel
    constexpr std::string_view kPicture(
        "BM\x3A\0\0\0\0\0\0\0\x36\0\0\0\x28\0\0\0\1\0\0\0\1\0\0\0\1\0\x18\0\0\0\0\0\4\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\xFF\0\0\0", 58);

    Project MakeProject(const TempFolder& temp)
    {
        const auto project = Project::Create(temp.Path(), "Game");
        REQUIRE(project);
        return *project;
    }

    std::string ReadBytes(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    nlohmann::ordered_json MainSceneSection(AssetID id, std::string_view path)
    {
        return { { "mainScene", { { "guid", id.ToString() }, { "path", path } } } };
    }

    // What the project file says the main scene's path is
    std::string MainScenePath(const fs::path& root)
    {
        return Project::Load(root)->GetSettings().at("mainScene").at("path").get<std::string>();
    }

    bool HasEntityNamed(Scene& scene, std::string_view name)
    {
        for (auto [handle, entityName] : scene.GetRegistry().view<NameComponent>().each())
        {
            if (entityName.Name == name)
                return true;
        }

        return false;
    }

    AssetID IdOf(Entity entity)
    {
        return entity.GetComponent<NameComponent>().Id;
    }

    // Apart from the fixture, so the files are there before the asset manager looks
    struct ProjectFiles
    {
        TempFolder Temp;
        Project Created = MakeProject(Temp);

        ProjectFiles()
        {
            Temp.Write("Game/assets/meshes/floor.obj", kQuad);
            Temp.Write("Game/assets/meshes/pyramid.obj", kQuad);
            Temp.Write("Game/assets/textures/picture.bmp", kPicture);
            Temp.Write("Game/assets/materials/floor.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl" })");
            Temp.Write("Game/assets/scenes/main.scene", kMainScene);
        }
    };

    struct Fixture : ProjectFiles
    {
        RecentProjects Recents{ Temp.Path() / "recent.json" };
        FakeGpuResourceManager Gpu;
        bool Editing = false;
        std::optional<ProjectSession> Open;

        Fixture()
        {
            Recents.Add(Created);
            Reopen();
        }

        ProjectSession& Session() { return *Open; }
        AssetManager& Assets() { return Open->GetAssets(); }
        Scene& World() { return Open->GetScene(); }

        // Gone without closing, like a killed process. The recovery files finish what they were writing.
        void Reopen()
        {
            Open.reset();
            Open.emplace(Created, Gpu, Recents, [this] { return Editing; });
        }

        SceneID MainScene() { return Assets().Find<SceneAsset>("scenes/main.scene"); }
        fs::path MainFile() const { return Created.AssetsDir() / "scenes/main.scene"; }
        fs::path CopyOf(SceneID id) const { return Created.DataDir() / "recovery" / (id.ToString() + ".scene"); }

        Entity Root(size_t index) { return World().GetEntity(World().GetRoots()[index]); }
    };
}

TEST_CASE_METHOD(Fixture, "The project opens on the scene it had open last", "[project session]")
{
    const SceneID main = MainScene();
    REQUIRE(main);
    CHECK(Session().GetSceneAsset() == main);
    CHECK(Session().TakeMissingAssets() == 0);

    const SceneID other = Assets().Create<SceneAsset>("scenes/other.scene");
    REQUIRE(other);

    Recents.SetLastScene(Created.Root(), other);
    Reopen();
    CHECK(Session().GetSceneAsset() == other);

    Session().OpenScene(main);
    Session().Update(0.0f);
    CHECK(Session().GetSceneAsset() == main);
    CHECK(Recents.GetLastScene(Created.Root()) == AssetID(main));
}

TEST_CASE_METHOD(Fixture, "A project with no scene gets one", "[project session]")
{
    fs::remove_all(Created.AssetsDir() / "scenes");
    Reopen();

    const SceneID main = MainScene();
    REQUIRE(main);

    CHECK(Session().GetSceneAsset() == main);
    CHECK(fs::exists(MainFile()));
    CHECK_FALSE(Session().CheckSceneChanged());
}

TEST_CASE_METHOD(Fixture, "A broken scene isn't saved over", "[project session]")
{
    constexpr std::string_view kBroken = "{ not json";

    const LogCounter errors(LogLevel::Error, "scenes/main.scene");
    Temp.Write("Game/assets/scenes/main.scene", kBroken);
    Reopen();

    CHECK_FALSE(Session().GetSceneAsset());
    CHECK(errors.Count() > 0);

    World().CreateEntity("Would be lost");
    CHECK_FALSE(Session().CheckSceneChanged());
    CHECK(Session().SaveAll());

    CHECK(ReadBytes(MainFile()) == kBroken);
}

TEST_CASE_METHOD(Fixture, "A scene with missing assets says how many, once", "[project session]")
{
    Temp.Write("Game/assets/scenes/main.scene", R"({ "version": 1, "entities": [
            { "name": "Lost", "meshRenderer": { "mesh": "meshes/gone.obj", "materials": [ "materials/gone.mat" ] } }
        ] })");
    Reopen();

    CHECK(Session().TakeMissingAssets() == 2);
    CHECK(Session().TakeMissingAssets() == 0);
}

TEST_CASE_METHOD(Fixture, "Selecting an entity and an asset replace each other", "[project session]")
{
    const Entity entity = World().CreateEntity("Thing");
    const MaterialID material = Assets().Find<MaterialAsset>("materials/floor.mat");

    Session().SelectEntity(entity);
    CHECK(Session().GetSelected() == entity);

    Session().SelectAsset(material);
    CHECK_FALSE(Session().GetSelected());
    CHECK(Session().GetSelectedAsset() == AssetID(material));

    Session().SelectEntity(entity);
    CHECK_FALSE(Session().GetSelectedAsset());
    CHECK(Session().GetSelected() == entity);
}

TEST_CASE_METHOD(Fixture, "A scene's changes show and can be saved", "[project session]")
{
    const SceneID id = MainScene();
    REQUIRE(Session().GetSceneAsset() == id);

    World().CreateEntity("New");

    SECTION("Saving writes the file and clears the mark")
    {
        CHECK(Session().CheckSceneChanged());
        REQUIRE(Session().SaveAll());

        CHECK_FALSE(Session().IsSceneChanged());
        CHECK_FALSE(Session().CheckSceneChanged());
        CHECK(Assets().Read(id)->Roots.back().Name == "New");
    }

    SECTION("The main camera is part of the scene")
    {
        const Entity camera = World().CreateEntity("Camera");
        camera.AddComponent<CameraComponent>();
        REQUIRE(Session().SaveAll());
        REQUIRE_FALSE(Session().CheckSceneChanged());

        Session().GetSettings().MainCamera = IdOf(camera);
        CHECK(Session().CheckSceneChanged());

        REQUIRE(Session().SaveAll());
        CHECK_FALSE(Session().CheckSceneChanged());

        // What a reopened scene is made from
        CHECK(Assets().Read(id)->Settings.MainCamera == IdOf(camera));
    }

    SECTION("The mark follows the timer")
    {
        Session().Update(ProjectSession::kSceneCheckSeconds / 2.0f);
        CHECK_FALSE(Session().IsSceneChanged());

        Session().Update(ProjectSession::kSceneCheckSeconds);
        CHECK(Session().IsSceneChanged());
    }
}

TEST_CASE_METHOD(Fixture, "Closing waits for an answer about unsaved changes", "[project session]")
{
    int closed = 0;

    Session().RequestClose([&] { ++closed; });
    CHECK(closed == 1);
    CHECK_FALSE(Session().IsClosePending());

    const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");

    MaterialData data = *Session().GetAssetEdits().Get(id);
    data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
    Session().GetAssetEdits().Set(id, data);
    Session().Update(0.0f);

    Session().RequestClose([&] { ++closed; });
    CHECK(closed == 1);
    REQUIRE(Session().IsClosePending());

    SECTION("Cancel stays")
    {
        Session().CancelClose();

        CHECK_FALSE(Session().IsClosePending());
        CHECK(closed == 1);
        CHECK(Session().GetAssetEdits().IsDirty(id));
    }

    SECTION("Discard closes without saving")
    {
        Session().DiscardAllAndClose();

        CHECK(closed == 2);
        CHECK_FALSE(Assets().Read(id)->Properties.contains("Color"));
    }

    SECTION("Save writes it and closes")
    {
        CHECK(Session().SaveAllAndClose());

        CHECK(closed == 2);
        CHECK(Assets().Read(id)->Properties.at("Color") == data.Properties.at("Color"));
    }
}

TEST_CASE_METHOD(Fixture, "Opening another scene asks first, and replaces the one showing", "[project session]")
{
    const SceneID main = MainScene();
    const SceneID other = Assets().Create<SceneAsset>("scenes/other.scene");
    REQUIRE(other);

    World().CreateEntity("Unsaved");
    Session().SelectEntity(Root(0));

    Session().OpenScene(main);
    CHECK_FALSE(Session().IsClosePending());

    Session().OpenScene(other);
    REQUIRE(Session().IsClosePending());
    CHECK(Session().GetSceneAsset() == main);

    // The scene changes between frames, as loading it creates GPU resources
    Session().DiscardAllAndClose();
    CHECK(Session().GetSceneAsset() == main);

    Session().Update(0.0f);

    CHECK(Session().GetSceneAsset() == other);
    CHECK_FALSE(Session().GetSelected());
    REQUIRE(World().GetRoots().size() == 1);
    CHECK(Root(0).GetComponent<NameComponent>().Name == "Sun");
    CHECK_FALSE(Session().GetHistory().CanUndo());
    CHECK_FALSE(Session().GetHistory().CanRedo());
    CHECK(Assets().Read(main)->Roots.back().Name == "Sun");
}

TEST_CASE_METHOD(Fixture, "Scene edits can be undone and redone", "[project session]")
{
    Scene& scene = World();
    REQUIRE(scene.GetRoots().size() >= 2);
    CHECK_FALSE(Session().GetHistory().CanUndo());

    const size_t roots = scene.GetRoots().size();
    const AssetID firstId = IdOf(Root(0));

    SECTION("A deleted entity comes back where it was, with its id, and the scene is as saved again")
    {
        Session().GetSceneEdits().RecordDestroy(Root(0));
        scene.DestroyEntity(Root(0));
        Session().GetHistory().Commit();
        REQUIRE(Session().GetHistory().CanUndo());

        Session().Undo();

        REQUIRE(scene.GetRoots().size() == roots);
        CHECK(IdOf(Root(0)) == firstId);
        CHECK_FALSE(Session().IsSceneChanged());

        Session().Redo();

        CHECK(scene.GetRoots().size() == roots - 1);
        CHECK(Session().IsSceneChanged());
    }

    SECTION("A deleted entity that was selected is selected again, by its id")
    {
        Session().SelectEntity(Root(0));
        Session().Update(0.0f);

        Session().GetSceneEdits().RecordDestroy(Root(0));
        scene.DestroyEntity(Root(0));
        Session().MarkEdited();
        Session().Update(0.0f);
        CHECK_FALSE(Session().GetSelected());

        Session().Undo();

        REQUIRE(Session().GetSelected());
        CHECK(IdOf(Session().GetSelected()) == firstId);
        CHECK(Session().GetSelected() == Root(0));

        // With nothing left to undo, the selection stays as it is
        REQUIRE_FALSE(Session().GetHistory().CanUndo());
        Session().SelectEntity({});
        Session().Undo();
        CHECK_FALSE(Session().GetSelected());
    }

    SECTION("The selected entity is recorded, so what the Inspector or the gizmo does to it is one step")
    {
        const Entity selected = Root(1);
        Session().SelectEntity(selected);
        Session().Update(0.0f);

        // Changed over a few updates, as a drag would
        for (int update = 0; update < 3; ++update)
        {
            selected.GetComponent<TransformComponent>().Position.x += 1.0f;
            Session().Update(0.0f);
        }

        Session().MarkEdited();
        Session().Update(0.0f);

        Session().Undo();

        // The same entity, as only it was put back
        CHECK(Session().GetSelected() == selected);
        CHECK(selected.GetComponent<TransformComponent>().Position.x == 0.0f);
        CHECK_FALSE(Session().GetHistory().CanUndo());
    }

    SECTION("Edits recorded between updates are each a step of their own")
    {
        Session().GetSceneEdits().RecordCreated(scene.CreateEntity("First"));
        Session().MarkEdited();
        Session().Update(0.0f);
        CHECK(Session().GetHistory().CanUndo());

        Session().GetSceneEdits().RecordCreated(scene.CreateEntity("Second"));
        Session().MarkEdited();
        Session().Update(0.0f);

        Session().Undo();
        CHECK(scene.GetRoots().size() == roots + 1);

        Session().Undo();
        CHECK(scene.GetRoots().size() == roots);
    }

    SECTION("Nothing is undone while something is being edited")
    {
        Session().GetSceneEdits().RecordCreated(scene.CreateEntity("First"));
        Session().MarkEdited();
        Session().Update(0.0f);

        Editing = true;
        Session().Undo();
        CHECK(scene.GetRoots().size() == roots + 1);

        Editing = false;
        Session().Undo();
        CHECK(scene.GetRoots().size() == roots);
    }

    SECTION("A step ends once editing stops")
    {
        const Entity selected = Root(1);
        Session().SelectEntity(selected);
        Session().Update(0.0f);

        Editing = true;
        selected.GetComponent<TransformComponent>().Position.x = 5.0f;
        Session().Update(0.0f);
        CHECK_FALSE(Session().GetHistory().CanUndo());

        // Editing just ended, so the edit may not be done with
        Editing = false;
        Session().Update(0.0f);
        CHECK_FALSE(Session().GetHistory().CanUndo());

        Session().Update(0.0f);
        CHECK(Session().GetHistory().CanUndo());

        Session().Undo();
        CHECK(selected.GetComponent<TransformComponent>().Position.x == 0.0f);
    }

    SECTION("Opening another scene forgets the steps")
    {
        Session().GetSceneEdits().RecordDestroy(Root(0));
        scene.DestroyEntity(Root(0));
        Session().GetHistory().Commit();
        REQUIRE(Session().SaveAll());

        const SceneID other = Assets().Create<SceneAsset>("scenes/other.scene");
        REQUIRE(other);

        Session().OpenScene(other);
        Session().Update(0.0f);

        REQUIRE(Session().GetSceneAsset() == other);
        CHECK_FALSE(Session().GetHistory().CanUndo());
        CHECK_FALSE(Session().GetHistory().CanRedo());
    }

    SECTION("Undoing a settings change puts the settings back")
    {
        Session().GetSceneEdits().RecordSettings();
        Session().GetSettings().PostProcessing.Exposure = 2.0f;
        Session().MarkEdited();
        Session().Update(0.0f);

        Session().Undo();
        CHECK(Session().GetSettings().PostProcessing.Exposure == PostProcessSettings{}.Exposure);

        Session().Redo();
        CHECK(Session().GetSettings().PostProcessing.Exposure == 2.0f);
    }
}

TEST_CASE_METHOD(Fixture, "A material's edits apply on Update, undo, redo and save", "[project session]")
{
    const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");
    const MaterialHandle handle = *Assets().Get(id);
    const MaterialData original = *Assets().GetData(id);

    Session().SelectAsset(id);

    MaterialData data = original;
    data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
    data.Textures["ColorMap"] = Assets().Find<TextureAsset>("textures/picture.bmp");
    Session().GetAssetEdits().Set(id, data);

    CHECK(*Assets().GetData(id) == original);
    CHECK(Session().GetAssetEdits().IsDirty(id));

    Session().Update(0.0f);

    CHECK(*Assets().Get(id) == handle);
    CHECK(*Assets().GetData(id) == data);
    CHECK(Session().GetAssetEdits().IsDirty(id));

    Session().Undo();
    Session().Update(0.0f);

    CHECK_FALSE(Assets().GetData(id)->Textures.contains("ColorMap"));
    CHECK_FALSE(Session().GetAssetEdits().IsDirty(id));

    Session().Redo();
    Session().Update(0.0f);

    CHECK(*Assets().GetData(id) == data);
    CHECK(Session().GetAssetEdits().IsDirty(id));

    REQUIRE(Session().SaveAll());
    CHECK_FALSE(Session().GetAssetEdits().IsDirty(id));
    CHECK(Assets().Read(id)->Properties.at("Color") == data.Properties.at("Color"));

    // A scene step undone with its entity still selected leaves no selection behind for a later step to undo into
    const Entity entity = Root(0);
    Session().SelectEntity(entity);
    Session().Update(0.0f);

    Session().GetSceneEdits().Record(entity);
    entity.GetComponent<TransformComponent>().Position.x += 1.0f;
    REQUIRE(Session().GetHistory().Commit());
    Session().Undo();
    Session().Redo();

    Session().SelectAsset(id);
    Session().Update(0.0f);

    MaterialData again = *Session().GetAssetEdits().Get(id);
    again.Properties["Color"] = { 0.0f, 0.0f, 1.0f, 1.0f };
    Session().GetAssetEdits().Set(id, again);
    Session().Undo();

    CHECK(Session().GetSelectedAsset() == AssetID(id));
    CHECK_FALSE(Session().GetSelected());
}

TEST_CASE_METHOD(Fixture, "Deleting assets", "[project session]")
{
    SECTION("The selected material")
    {
        const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");

        MaterialData data = *Session().GetAssetEdits().Get(id);
        data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
        Session().GetAssetEdits().Set(id, data);
        Session().SelectAsset(id);
        REQUIRE(Session().GetAssetEdits().IsDirty(id));

        REQUIRE(Assets().Delete(id));
        const std::array<AssetID, 1> removed = { id };
        Session().ForgetAssets(removed);

        CHECK_FALSE(Session().GetSelectedAsset());
        CHECK_FALSE(Session().GetAssetEdits().IsDirty(id));
    }

    SECTION("The open scene with unsaved changes")
    {
        const SceneID main = MainScene();
        const SceneID other = Assets().Create<SceneAsset>("scenes/other.scene");
        REQUIRE(other);

        Session().OpenScene(other);
        Session().Update(0.0f);
        REQUIRE(Session().GetSceneAsset() == other);

        World().CreateEntity("Unsaved");
        Session().WriteRecovery();

        // Reopening finishes the write, and a copy that is offered was there. The scene open is the one that was last.
        Reopen();
        REQUIRE(Session().GetSceneAsset() == other);
        REQUIRE(Session().GetOfferedRecovery());
        REQUIRE(fs::exists(CopyOf(other)));

        World().CreateEntity("Unsaved again");
        REQUIRE(Session().CheckSceneChanged());

        REQUIRE(Assets().Delete(other));
        const std::array<AssetID, 1> removed = { other };
        Session().ForgetAssets(removed);

        CHECK(Session().GetSceneAsset() == main);
        CHECK_FALSE(Session().IsClosePending());
        CHECK_FALSE(fs::exists(CopyOf(other)));
    }

    SECTION("The only scene")
    {
        const SceneID id = MainScene();

        REQUIRE(Assets().Delete(id));
        const std::array<AssetID, 1> removed = { id };
        Session().ForgetAssets(removed);

        CHECK(Session().GetSceneAsset());
        CHECK(Session().GetSceneAsset() != id);
    }

    SECTION("An instance's source")
    {
        Temp.Write("Game/assets/scenes/crate.scene", R"({ "version": 1, "entities": [ { "name": "Crate", "meshRenderer": { "mesh": "meshes/floor.obj" } } ] })");
        Assets().Refresh();

        const SceneID source = Assets().Find<SceneAsset>("scenes/crate.scene");
        REQUIRE(source);

        const Entity instance = CreateSceneInstance(source, World(), {}, Assets(), Session().GetSceneAsset());
        REQUIRE(instance);
        REQUIRE(instance.GetComponent<RelationshipComponent>().Children.size() == 1);

        const Entity child = World().GetEntity(instance.GetComponent<RelationshipComponent>().Children[0]);

        REQUIRE(Assets().Delete(source));
        const std::array<AssetID, 1> removed = { source };
        Session().ForgetAssets(removed);

        CHECK(instance);
        CHECK_FALSE(child);
        CHECK(Assets().IsMissing(instance.GetComponent<SceneInstanceComponent>().Scene));
    }
}

TEST_CASE_METHOD(Fixture, "Unsaved changes are copied on a timer, and offered after a crash", "[project session]")
{
    const SceneID id = MainScene();
    const std::string savedBytes = ReadBytes(MainFile());

    World().CreateEntity("Unsaved");
    Session().Update(ProjectSession::kRecoverySeconds);

    // The recovery files finish what they were writing
    Reopen();

    REQUIRE(fs::exists(CopyOf(id)));
    REQUIRE(Session().GetOfferedRecovery());
    CHECK(Session().GetSceneAsset() == id);
    CHECK_FALSE(Session().CheckSceneChanged());

    Session().RestoreRecovery();
    Session().Update(0.0f);

    CHECK_FALSE(Session().GetOfferedRecovery());
    CHECK(Session().IsSceneChanged());
    CHECK(HasEntityNamed(World(), "Unsaved"));
    CHECK(ReadBytes(MainFile()) == savedBytes);

    REQUIRE(Session().SaveAll());
    CHECK(Assets().Read(id)->Roots.back().Name == "Unsaved");
    CHECK_FALSE(fs::exists(CopyOf(id)));
}

TEST_CASE_METHOD(Fixture, "A copy goes once the scene is back to its file, or on closing", "[project session]")
{
    const SceneID id = MainScene();

    World().CreateEntity("Unsaved");
    Session().WriteRecovery();

    // Reopening finishes the write, and a copy that is offered was there. Restoring it makes it this session's own.
    Reopen();
    REQUIRE(Session().GetOfferedRecovery());
    REQUIRE(fs::exists(CopyOf(id)));

    Session().RestoreRecovery();
    Session().Update(0.0f);
    REQUIRE(HasEntityNamed(World(), "Unsaved"));

    SECTION("Back to what the file says")
    {
        World().DestroyEntity(Root(World().GetRoots().size() - 1));
        Session().WriteRecovery();

        CHECK_FALSE(fs::exists(CopyOf(id)));
    }

    SECTION("Closing without saving")
    {
        int closed = 0;
        Session().RequestClose([&] { ++closed; });
        REQUIRE(Session().IsClosePending());

        Session().DiscardAllAndClose();

        CHECK(closed == 1);
        CHECK_FALSE(fs::exists(CopyOf(id)));
    }
}

TEST_CASE_METHOD(Fixture, "A scene file without ids keeps the ids it is given", "[project session]")
{
    Temp.Write("Game/assets/scenes/main.scene", R"({ "version": 1, "entities": [ { "name": "Floor" }, { "name": "Sun", "light": { "type": "directional" } } ] })");
    Reopen();

    const SceneID id = MainScene();

    SECTION("A restored copy counts as unchanged once its changes are undone")
    {
        World().CreateEntity("Unsaved");
        Session().WriteRecovery();

        Reopen();
        REQUIRE(Session().GetOfferedRecovery());
        REQUIRE(fs::exists(CopyOf(id)));

        Session().RestoreRecovery();
        Session().Update(0.0f);
        REQUIRE(HasEntityNamed(World(), "Unsaved"));

        World().DestroyEntity(Root(World().GetRoots().size() - 1));
        Session().WriteRecovery();

        CHECK_FALSE(Session().CheckSceneChanged());
        CHECK_FALSE(fs::exists(CopyOf(id)));
    }

    SECTION("The main camera survives saving and opening again")
    {
        const Entity camera = World().CreateEntity("Camera");
        camera.AddComponent<CameraComponent>();
        Session().GetSettings().MainCamera = IdOf(camera);
        REQUIRE(Session().SaveAll());

        Reopen();

        const AssetID main = Session().GetSettings().MainCamera;
        REQUIRE(main);

        bool found = false;

        for (auto [handle, name] : World().GetRegistry().view<NameComponent>().each())
        {
            if (name.Name == "Camera")
            {
                found = true;
                CHECK(name.Id == main);
            }
        }

        CHECK(found);
    }
}

TEST_CASE_METHOD(Fixture, "Discarding a recovery copy keeps the file, and a stale one goes without asking", "[project session]")
{
    const SceneID main = MainScene();
    const SceneID other = Assets().Create<SceneAsset>("scenes/other.scene");
    REQUIRE(other);

    const fs::path otherFile = Created.AssetsDir() / "scenes/other.scene";
    const std::string otherBytes = ReadBytes(otherFile);

    // An hour newer than its file, as two writes close together can get the same time
    const fs::path otherCopy = Temp.Write("Game/.crimson/recovery/" + other.ToString() + ".scene",
                                          R"({ "version": 1, "entities": [ { "name": "From the copy" } ] })");
    fs::last_write_time(otherCopy, fs::last_write_time(otherFile) + std::chrono::hours(1));

    // An hour older than its file
    const fs::path mainCopy = Temp.Write("Game/.crimson/recovery/" + main.ToString() + ".scene",
                                         R"({ "version": 1, "entities": [ { "name": "Too old" } ] })");
    fs::last_write_time(mainCopy, fs::last_write_time(MainFile()) - std::chrono::hours(1));

    Session().OpenScene(other);
    Session().Update(0.0f);

    REQUIRE(Session().GetSceneAsset() == other);
    REQUIRE(Session().GetOfferedRecovery());

    Session().DiscardRecovery();
    CHECK_FALSE(Session().GetOfferedRecovery());
    CHECK_FALSE(fs::exists(otherCopy));
    CHECK_FALSE(Session().CheckSceneChanged());
    CHECK_FALSE(HasEntityNamed(World(), "From the copy"));
    CHECK(ReadBytes(otherFile) == otherBytes);

    Session().OpenScene(main);
    Session().Update(0.0f);

    CHECK(Session().GetSceneAsset() == main);
    CHECK_FALSE(Session().GetOfferedRecovery());
    CHECK_FALSE(fs::exists(mainCopy));
}

TEST_CASE_METHOD(Fixture, "A copy that can't be read is deleted", "[project session]")
{
    const SceneID id = MainScene();

    const fs::path copy = Temp.Write("Game/.crimson/recovery/" + id.ToString() + ".scene", "{ not json");
    fs::last_write_time(copy, fs::last_write_time(MainFile()) + std::chrono::hours(1));

    Reopen();
    REQUIRE(Session().GetOfferedRecovery());

    const LogCounter warnings(LogLevel::Warn, "can't be read");
    Session().RestoreRecovery();
    Session().Update(0.0f);

    CHECK(warnings.Count() == 1);
    CHECK_FALSE(fs::exists(copy));
    CHECK_FALSE(Session().GetOfferedRecovery());
}

TEST_CASE_METHOD(Fixture, "A project's main scene is kept, and followed when it moves", "[project session]")
{
    const SceneID main = MainScene();
    const SceneID other = Assets().Create<SceneAsset>("scenes/other.scene");
    REQUIRE(other);
    CHECK_FALSE(Session().GetProjectSettings().MainScene);

    Session().SetProjectSettings({ .MainScene = other });
    CHECK(Session().GetProjectSettings().MainScene == other);

    SECTION("It is in the file, and the scene the editor opens stays the user's")
    {
        Recents.SetLastScene(Created.Root(), main);
        Open.reset();
        Open.emplace(*Project::Load(Created.Root()), Gpu, Recents, [this] { return Editing; });

        CHECK(Session().GetProjectSettings().MainScene == other);
        CHECK(Session().GetSceneAsset() == main);
    }

    SECTION("Moving the scene keeps it, with the new path")
    {
        REQUIRE(Assets().Rename(other, "scenes/moved.scene"));
        Session().Update(0.0f);

        CHECK(Session().GetProjectSettings().MainScene == other);
        CHECK(MainScenePath(Created.Root()) == "scenes/moved.scene");
    }

    SECTION("A stale id is found by the path")
    {
        Project edited = *Project::Load(Created.Root());
        REQUIRE(edited.SetSettings(MainSceneSection(AssetID::Generate(), "scenes/other.scene")));

        Open.reset();
        Open.emplace(*Project::Load(Created.Root()), Gpu, Recents, [this] { return Editing; });

        CHECK(Session().GetProjectSettings().MainScene == other);
        CHECK((*Project::Load(Created.Root())).GetSettings().at("mainScene").at("guid") == AssetID(other).ToString());
    }

    SECTION("Deleting it keeps it as missing")
    {
        const LogCounter warnings(LogLevel::Warn, "main scene");

        REQUIRE(Assets().Delete(other));
        const std::array removed = { AssetID(other) };
        Session().ForgetAssets(removed);
        Session().Update(0.0f);

        CHECK(Session().GetProjectSettings().MainScene == other);
        CHECK(Assets().IsMissing(other));
        CHECK(AssetFieldText(Assets(), other, AssetType::Of<SceneAsset>()) == "Missing: scenes/other.scene");
        CHECK(MainScenePath(Created.Root()) == "scenes/other.scene");
        CHECK(Project::Load(Created.Root())->GetSettings().at("mainScene").at("guid") == AssetID(other).ToString());
        CHECK(warnings.Count() == 0);
    }

    SECTION("Setting none clears it, without a warning")
    {
        const LogCounter warnings(LogLevel::Warn, "main scene");

        Session().SetProjectSettings({});

        CHECK_FALSE(Session().GetProjectSettings().MainScene);
        CHECK(warnings.Count() == 0);
        CHECK_FALSE(Project::Load(Created.Root())->GetSettings().contains("mainScene"));
    }

    SECTION("Setting the same settings again leaves the file alone")
    {
        const std::string before = ReadBytes(Created.Root() / Project::kFileName);

        Session().SetProjectSettings({ .MainScene = other });

        CHECK(ReadBytes(Created.Root() / Project::kFileName) == before);
    }
}

TEST_CASE_METHOD(Fixture, "A main scene that is missing at open is kept as missing, with a warning", "[project session]")
{
    const AssetID guid = AssetID::Generate();

    Project edited = *Project::Load(Created.Root());
    REQUIRE(edited.SetSettings(MainSceneSection(guid, "scenes/gone.scene")));
    const std::string before = ReadBytes(Created.Root() / Project::kFileName);

    const LogCounter warnings(LogLevel::Warn, "main scene");
    const LogCounter errors(LogLevel::Error, "");
    Open.reset();
    Open.emplace(*Project::Load(Created.Root()), Gpu, Recents, [this] { return Editing; });

    const SceneID missing = Session().GetProjectSettings().MainScene;
    CHECK(AssetID(missing) == guid);
    CHECK(Assets().IsMissing(missing));
    CHECK(AssetFieldText(Assets(), missing, AssetType::Of<SceneAsset>()) == "Missing: scenes/gone.scene");
    CHECK(warnings.Count() == 1);
    CHECK(errors.Count() == 0);
    CHECK(ReadBytes(Created.Root() / Project::kFileName) == before);

    SECTION("It is found again once the scene is back")
    {
        const SceneID back = Assets().Create<SceneAsset>("scenes/gone.scene");
        REQUIRE(back);
        Session().Update(0.0f);

        CHECK(Session().GetProjectSettings().MainScene == back);
        CHECK_FALSE(Assets().IsMissing(Session().GetProjectSettings().MainScene));
        CHECK(Project::Load(Created.Root())->GetSettings().at("mainScene").at("guid") == AssetID(back).ToString());
    }
}
