#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/core/log.hpp>
#include <crimson/project/project.hpp>
#include <crimson/scene/scene_serializer.hpp>
#include <crimson_editor/editor_application.hpp>
#include <crimson_editor/project_manager_mode.hpp>
#include <crimson_editor/project_session_mode.hpp>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;
using crimson::editor::EditorApplication;
using crimson::editor::EditorContext;
using crimson::editor::ProjectManagerMode;
using crimson::editor::ProjectSessionMode;

namespace
{
    constexpr std::string_view kQuad = "v -1 0 1\nv 1 0 1\nv 1 0 -1\nv -1 0 -1\nvn 0 1 0\nf 1//1 2//1 3//1 4//1\n";

    constexpr int kFrames = 4;

    void RunFrames(EditorApplication& app, int frames = kFrames)
    {
        for (int i = 0; i < frames; ++i)
            app.RunFrame();
    }

    template <typename Mode>
    bool IsShowing(const EditorApplication& app)
    {
        return dynamic_cast<const Mode*>(&app.CurrentMode()) != nullptr;
    }

    // A project with some meshes and a scene using them, and an empty one otherwise
    Project MakeProject(const TempFolder& temp, const char* name, bool withMeshes)
    {
        const auto project = Project::Create(temp.Path(), name);
        REQUIRE(project);

        if (withMeshes)
        {
            temp.Write(std::string(name) + "/assets/meshes/floor.obj", kQuad);
            temp.Write(std::string(name) + "/assets/meshes/pyramid.obj", kQuad);
            temp.Write(std::string(name) + "/assets/materials/floor.mat", R"({ "shader": "engine/shaders/main.hlsl" })");
            temp.Write(std::string(name) + "/assets/scenes/main.scene", R"({ "version": 1, "entities": [
                { "name": "Floor", "meshRenderer": { "mesh": "meshes/floor.obj", "materials": [ "materials/floor.mat" ] } },
                { "name": "Pyramid", "meshRenderer": { "mesh": "meshes/pyramid.obj" } },
                { "name": "Sun", "light": { "type": "directional" } }
            ] })");
        }

        return *project;
    }
}

// The editor brings its own window and backend, and each of these tests starts one
TEST_CASE("The editor starts on the project manager", "[editor]")
{
    ShutdownGpu();

    EditorApplication app(SelectedBackend(), {});
    RunFrames(app);

    CHECK(IsShowing<ProjectManagerMode>(app));
}

TEST_CASE("The editor opens a project and draws it", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    EditorApplication app(SelectedBackend(), {});
    RunFrames(app);

    app.Services().OpenProject(MakeProject(temp, "Game", true));

    // It only changes after the frame
    CHECK(IsShowing<ProjectManagerMode>(app));

    RunFrames(app);

    CHECK(IsShowing<ProjectSessionMode>(app));
    CHECK(app.Services().Graphics.Renderer->GetStats().DrawCalls > 0);

    // Bigger than the project manager's 1000. Not exactly the editor's 1280, as Windows limits a window to the screen.
    CHECK(app.Services().AppWindow.Width() > 1000);
}

TEST_CASE("Starting with --project goes straight to it", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Game", false);

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    CHECK(IsShowing<ProjectSessionMode>(app));
}

TEST_CASE("Starting with a bad --project shows the project manager and the error", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;

    EditorApplication app(SelectedBackend(), temp.Path() / "nothing");
    RunFrames(app);

    CHECK(IsShowing<ProjectManagerMode>(app));

    CHECK(ExpectLoggedErrors("No project.crimson") == 1);
}

// What a GPU resource left behind by the session, or a dangling log subscriber, would break
TEST_CASE("Projects can be opened and closed again and again", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project withMeshes = MakeProject(temp, "Meshes", true);
    const Project empty = MakeProject(temp, "Empty", false);

    EditorApplication app(SelectedBackend(), {});
    RunFrames(app);

    for (int i = 0; i < 10; ++i)
    {
        CAPTURE(i);

        app.Services().OpenProject(i % 2 == 0 ? withMeshes : empty);
        RunFrames(app);
        REQUIRE(IsShowing<ProjectSessionMode>(app));

        app.Services().CloseProject();
        RunFrames(app);
        REQUIRE(IsShowing<ProjectManagerMode>(app));
    }

    // The console was destroyed with the project, so this would call through a dangling pointer
    LOG_INFO("Still logging after closing the project");
}

TEST_CASE("One project can replace another directly", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;

    EditorApplication app(SelectedBackend(), MakeProject(temp, "First", true).Root());
    RunFrames(app);

    app.Services().OpenProject(MakeProject(temp, "Second", false));
    RunFrames(app);

    CHECK(IsShowing<ProjectSessionMode>(app));
}

namespace
{
    // A project with one of each kind of asset, and the session showing it
    struct AssetFixture
    {
        TempFolder Temp;
        Project OpenProject;
        EditorApplication App;

        AssetFixture()
            : OpenProject(MakeAssetProject(Temp)), App(SelectedBackend(), OpenProject.Root())
        {
            RunFrames(App);
            REQUIRE(IsShowing<ProjectSessionMode>(App));
        }

        static Project MakeAssetProject(const TempFolder& temp)
        {
            ShutdownGpu();

            Project project = MakeProject(temp, "Assets", true);
            temp.Write("Assets/assets/textures/picture.bmp", std::string_view(
                "BM\x3A\0\0\0\0\0\0\0\x36\0\0\0\x28\0\0\0\1\0\0\0\1\0\0\0\1\0\x18\0\0\0\0\0\4\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\xFF\0\0\0", 58));
            temp.Write("Assets/assets/shaders/own.hlsl", "#include \"crimson/bindings.hlsli\"\n"
                "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }\n"
                "float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }\n");
            return project;
        }

        ProjectSessionMode& Session() { return dynamic_cast<ProjectSessionMode&>(App.CurrentMode()); }
        EditorContext& Context() { return Session().GetContext(); }
        AssetManager& Assets() { return Context().Assets; }
    };
}

// The panels draw whatever is selected on every frame, so asserts and validation errors in those views show up here
TEST_CASE_METHOD(AssetFixture, "Every kind of asset can be selected", "[editor]")
{
    for (const char* path : { "materials/floor.mat", "textures/picture.bmp", "meshes/floor.obj", "shaders/own.hlsl", "scenes/main.scene",
                              "engine/materials/default.mat", "engine/shaders/main.hlsl" })
    {
        CAPTURE(path);

        const AssetID id = Assets().FindId(path);
        REQUIRE(id);

        Context().SelectAsset(id);
        RunFrames(App);

        CHECK(Context().SelectedAsset == id);
    }
}

TEST_CASE_METHOD(AssetFixture, "Selecting an entity and an asset replace each other", "[editor]")
{
    const Entity entity = Context().ActiveScene.CreateEntity("Thing");

    Context().SelectEntity(entity);
    Context().SelectAsset(Assets().FindId("materials/floor.mat"));
    CHECK_FALSE(Context().Selected);

    Context().SelectEntity(entity);
    CHECK_FALSE(Context().SelectedAsset);
}

TEST_CASE_METHOD(AssetFixture, "A material's edits show and can be saved or reverted", "[editor]")
{
    const AssetID id = Assets().FindId("materials/floor.mat");
    const MaterialHandle handle = Assets().LoadMaterial(id);

    Context().SelectAsset(id);
    RunFrames(App);

    MaterialData data = *Context().Edits.GetMaterial(id);
    data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
    data.Textures["ColorMap"] = Assets().FindId("textures/picture.bmp");
    Context().Edits.SetMaterial(id, data);
    RunFrames(App);

    CHECK(Assets().LoadMaterial(id) == handle);
    CHECK(*Assets().GetMaterialData(id) == data);
    CHECK(Context().Edits.IsDirty(id));

    SECTION("Reverting")
    {
        Context().Edits.Revert(id);
        RunFrames(App);
        CHECK_FALSE(Assets().GetMaterialData(id)->Textures.contains("ColorMap"));
    }

    SECTION("Saving")
    {
        REQUIRE(Context().Edits.Save(id));
        CHECK(Assets().ReadMaterialFile(id)->Properties.at("Color") == data.Properties.at("Color"));
    }
}

TEST_CASE_METHOD(AssetFixture, "Changing a material's shader keeps the scene drawing", "[editor]")
{
    const AssetID id = Assets().FindId("materials/floor.mat");
    const MaterialHandle before = Assets().LoadMaterial(id);

    MaterialData data = *Context().Edits.GetMaterial(id);
    data.Shader = Assets().FindId("shaders/own.hlsl");
    Context().Edits.SetMaterial(id, data);
    RunFrames(App);

    CHECK(Assets().LoadMaterial(id) != before);

    // The floor uses this material, and it still has a valid handle or a draw would have failed
    CHECK(App.Services().Graphics.Renderer->GetStats().DrawCalls > 0);
}

TEST_CASE_METHOD(AssetFixture, "Reimporting a texture a panel is showing", "[editor]")
{
    const AssetID id = Assets().FindId("textures/picture.bmp");

    Context().SelectAsset(id);
    RunFrames(App);

    nlohmann::json settings = Assets().GetInfo(id)->Settings;
    settings["flipY"] = true;

    Context().Deferred.emplace_back([&] { Context().Thumbnails.Release(id); Assets().ReimportTexture(id, settings); });
    RunFrames(App);

    CHECK(Assets().GetInfo(id)->Settings.at("flipY") == true);
}

TEST_CASE_METHOD(AssetFixture, "Deleting assets the scene is using", "[editor]")
{
    SECTION("A material: the entity falls back to the default one")
    {
        const AssetID id = Assets().FindId("materials/floor.mat");
        Context().SelectAsset(id);
        RunFrames(App);

        Context().Deferred.emplace_back([&]
        {
            REQUIRE(Assets().Delete(id));
            const std::array removed = { id };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK_FALSE(Context().SelectedAsset);
        CHECK(App.Services().Graphics.Renderer->GetStats().DrawCalls > 0);
    }

    SECTION("A mesh: the entity stops drawing it")
    {
        const AssetID id = Assets().FindId("meshes/floor.obj");

        Context().Deferred.emplace_back([&]
        {
            REQUIRE(Assets().Delete(id));
            const std::array removed = { id };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK(IsShowing<ProjectSessionMode>(App));
    }

    SECTION("A texture a material is showing, and a shader")
    {
        const AssetID material = Assets().FindId("materials/floor.mat");

        MaterialData data = *Context().Edits.GetMaterial(material);
        data.Textures["ColorMap"] = Assets().FindId("textures/picture.bmp");
        Context().Edits.SetMaterial(material, data);
        Context().SelectAsset(material);
        RunFrames(App);

        Context().Deferred.emplace_back([&]
        {
            const AssetID texture = Assets().FindId("textures/picture.bmp");
            REQUIRE(Assets().Delete(texture));
            const std::array removed = { texture };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        // Kept as a missing one, for it to work again if the file comes back
        const AssetID texture = Assets().GetMaterialData(material)->Textures.at("ColorMap");
        CHECK(Assets().IsMissing(texture));
    }

    SECTION("Through a refresh, when the file went away by itself")
    {
        std::filesystem::remove(Temp.Path() / "Assets/assets/materials/floor.mat");

        Context().Deferred.emplace_back([&]
        {
            const std::vector<AssetID> removed = Assets().Refresh();
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK_FALSE(Assets().FindId("materials/floor.mat"));
    }
}

TEST_CASE_METHOD(AssetFixture, "Closing with unsaved changes asks first", "[editor]")
{
    const AssetID id = Assets().FindId("materials/floor.mat");

    MaterialData data = *Context().Edits.GetMaterial(id);
    data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
    Context().Edits.SetMaterial(id, data);
    RunFrames(App);

    App.RequestQuit();
    RunFrames(App);

    CHECK(App.IsRunning());
    REQUIRE(Session().IsAskingAboutUnsavedChanges());

    SECTION("Cancel stays")
    {
        Session().CancelClose();
        RunFrames(App);

        CHECK(App.IsRunning());
        CHECK(Context().Edits.IsDirty(id));
    }

    SECTION("Discard closes without saving")
    {
        const std::string before = std::string(Assets().ReadMaterialFile(id)->Properties.contains("Color") ? "had" : "none");
        Session().DiscardAllAndClose();

        CHECK_FALSE(App.IsRunning());
        CHECK(std::string(Assets().ReadMaterialFile(id)->Properties.contains("Color") ? "had" : "none") == before);
    }

    SECTION("Save writes it and closes")
    {
        Session().SaveAllAndClose();

        CHECK_FALSE(App.IsRunning());
        CHECK(Assets().ReadMaterialFile(id)->Properties.at("Color") == data.Properties.at("Color"));
    }
}

TEST_CASE_METHOD(AssetFixture, "A scene's changes show and can be saved", "[editor]")
{
    const AssetID id = Assets().FindId("scenes/main.scene");
    REQUIRE(Context().ActiveSceneAsset == id);
    CHECK_FALSE(Session().IsSceneChanged());

    Context().ActiveScene.CreateEntity("New");
    CHECK(Session().IsSceneChanged());

    SECTION("Saving writes it")
    {
        REQUIRE(Session().SaveAll());

        CHECK_FALSE(Session().IsSceneChanged());
        CHECK(Assets().ReadSceneFile(id)->Roots.back().Name == "New");
    }

    SECTION("Closing asks first")
    {
        App.RequestQuit();
        RunFrames(App);
        REQUIRE(Session().IsAskingAboutUnsavedChanges());

        Session().SaveAllAndClose();

        CHECK_FALSE(App.IsRunning());
        CHECK(Assets().ReadSceneFile(id)->Roots.back().Name == "New");
    }

    SECTION("Opening another scene asks first, and discarding leaves the file")
    {
        AssetID other;
        Context().Deferred.emplace_back([&] { other = Assets().CreateScene("scenes/other.scene"); });
        RunFrames(App);
        REQUIRE(other);

        Context().OpenScene(other);
        RunFrames(App);
        REQUIRE(Session().IsAskingAboutUnsavedChanges());
        CHECK(Context().ActiveSceneAsset == id);

        Session().DiscardAllAndClose();
        RunFrames(App);

        CHECK(Context().ActiveSceneAsset == other);
        CHECK_FALSE(Session().IsSceneChanged());
        CHECK(Assets().ReadSceneFile(id)->Roots.back().Name == "Sun");
    }
}

TEST_CASE_METHOD(AssetFixture, "Opening a scene replaces the one showing", "[editor]")
{
    const Entity old = Context().ActiveScene.GetEntity(Context().ActiveScene.GetRoots()[0]);
    Context().SelectEntity(old);

    AssetID other;
    Context().Deferred.emplace_back([&] { other = Assets().CreateScene("scenes/other.scene"); });
    RunFrames(App);

    Context().OpenScene(other);
    RunFrames(App);

    CHECK(Context().ActiveSceneAsset == other);
    CHECK_FALSE(Context().Selected);
    REQUIRE(Context().ActiveScene.GetRoots().size() == 1);
    CHECK(Context().ActiveScene.GetEntity(Context().ActiveScene.GetRoots()[0]).GetComponent<NameComponent>().Name == "Sun");

    SECTION("It is the one that opens next time")
    {
        App.Services().CloseProject();
        RunFrames(App);
        App.Services().OpenProject(OpenProject);
        RunFrames(App);

        CHECK(Context().ActiveSceneAsset == other);
    }

    SECTION("Deleting it goes on to another one, without asking")
    {
        Context().ActiveScene.CreateEntity("Unsaved");

        Context().Deferred.emplace_back([&]
        {
            REQUIRE(Assets().Delete(other));
            const std::array removed = { other };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK(Context().ActiveSceneAsset == Assets().FindId("scenes/main.scene"));
        CHECK_FALSE(Session().IsAskingAboutUnsavedChanges());
    }
}

TEST_CASE_METHOD(AssetFixture, "Deleting the only scene makes a new one", "[editor]")
{
    const AssetID id = Assets().FindId("scenes/main.scene");

    Context().Deferred.emplace_back([&]
    {
        REQUIRE(Assets().Delete(id));
        const std::array removed = { id };
        Context().OnAssetsRemoved(removed);
    });
    RunFrames(App);

    CHECK(Context().ActiveSceneAsset);
    CHECK(Context().ActiveSceneAsset != id);
    CHECK(IsShowing<ProjectSessionMode>(App));
}

TEST_CASE_METHOD(AssetFixture, "A scene's instances draw, and go empty without their source", "[editor]")
{
    AssetID source;
    Entity instance;

    Temp.Write("Assets/assets/scenes/crate.scene", R"({ "version": 1, "entities": [ { "name": "Crate", "meshRenderer": { "mesh": "meshes/floor.obj" } } ] })");

    Context().Deferred.emplace_back([&]
    {
        Assets().Refresh();
        source = Assets().FindId("scenes/crate.scene");
        instance = CreateSceneInstance(source, Context().ActiveScene, {}, Assets(), Context().ActiveSceneAsset);
    });
    RunFrames(App);

    REQUIRE(instance);
    REQUIRE(instance.GetComponent<RelationshipComponent>().Children.size() == 1);

    const uint32_t drawCalls = App.Services().Graphics.Renderer->GetStats().DrawCalls;
    CHECK(drawCalls > 0);

    // Panels draw what is selected, read-only or not
    const Entity child = Context().ActiveScene.GetEntity(instance.GetComponent<RelationshipComponent>().Children[0]);
    Context().SelectEntity(child);
    RunFrames(App);
    Context().SelectEntity(instance);
    RunFrames(App);

    Context().Deferred.emplace_back([&]
    {
        REQUIRE(Assets().Delete(source));
        const std::array removed = { source };
        Context().OnAssetsRemoved(removed);
    });
    RunFrames(App);

    CHECK(instance);
    CHECK_FALSE(child);
    CHECK(Assets().IsMissing(instance.GetComponent<SceneInstanceComponent>().Scene));
    CHECK(App.Services().Graphics.Renderer->GetStats().DrawCalls < drawCalls);
}

TEST_CASE("A project with no scene gets one", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    EditorApplication app(SelectedBackend(), MakeProject(temp, "Empty", false).Root());
    RunFrames(app);

    auto& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode());

    CHECK(session.GetContext().ActiveSceneAsset == session.GetContext().Assets.FindId("scenes/main.scene"));
    CHECK(std::filesystem::exists(temp.Path() / "Empty/assets/scenes/main.scene"));
    CHECK_FALSE(session.IsSceneChanged());
}

TEST_CASE("A broken scene isn't saved over", "[editor]")
{
    ShutdownGpu();

    constexpr std::string_view kBroken = "{ not json";

    const TempFolder temp;
    const Project project = MakeProject(temp, "Broken", false);
    const std::filesystem::path file = temp.Write("Broken/assets/scenes/main.scene", kBroken);

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    auto& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode());
    EditorContext& context = session.GetContext();

    CHECK_FALSE(context.ActiveSceneAsset);
    CHECK(ExpectLoggedErrors("scenes/main.scene") > 0);

    context.ActiveScene.CreateEntity("Would be lost");
    CHECK_FALSE(session.IsSceneChanged());
    CHECK(session.SaveAll());

    std::ifstream stream(file, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()) == kBroken);

    RunFrames(app);
}

TEST_CASE_METHOD(AssetFixture, "Closing with nothing unsaved doesn't ask", "[editor]")
{
    App.RequestQuit();
    CHECK_FALSE(App.IsRunning());
}

TEST_CASE("A project with a broken shader still opens", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Broken", true);
    temp.Write("Broken/assets/broken.hlsl", "this does not compile");
    temp.Write("Broken/assets/materials/floor.mat", R"({ "shader": "broken.hlsl" })");

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    CHECK(IsShowing<ProjectSessionMode>(app));

    CHECK(ExpectLoggedErrors("broken.hlsl") > 0);
    CHECK(ExpectLoggedErrors("materials/floor.mat") > 0);
}
