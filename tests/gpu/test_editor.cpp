#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <crimson/core/log.hpp>
#include <crimson/project/project.hpp>
#include <crimson/asset_manager/import_cache.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/mesh_asset.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <crimson/scene/scene_serializer.hpp>
#include <crimson_editor/editor_application.hpp>
#include <crimson_editor/editor_settings.hpp>
#include <crimson_editor/project_manager_mode.hpp>
#include <crimson_editor/project_session_mode.hpp>
#include <crimson_editor/ui/asset_field.hpp>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;
using crimson::editor::EditorApplication;
using crimson::editor::EditorContext;
using crimson::editor::EditorSettings;
using crimson::editor::ProjectManagerMode;
using crimson::editor::ProjectSessionMode;

constexpr std::string_view kQuad = "v -1 0 1\nv 1 0 1\nv 1 0 -1\nv -1 0 -1\nvn 0 1 0\nf 1//1 2//1 3//1 4//1\n";

constexpr int kFrames = 4;

static void RunFrames(EditorApplication& app, int frames = kFrames)
{
    for (int i = 0; i < frames; ++i)
        app.RunFrame();
}

template <typename Mode>
static bool IsShowing(const EditorApplication& app)
{
    return dynamic_cast<const Mode*>(&app.CurrentMode()) != nullptr;
}

// A project with some meshes and a scene using them, and an empty one otherwise
static Project MakeProject(const TempFolder& temp, const char* name, bool withMeshes)
{
    const auto project = Project::Create(temp.Path(), name);
    REQUIRE(project);

    if (withMeshes)
    {
        temp.Write(std::string(name) + "/assets/meshes/floor.obj", kQuad);
        temp.Write(std::string(name) + "/assets/meshes/pyramid.obj", kQuad);
        temp.Write(std::string(name) + "/assets/materials/floor.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl" })");
        temp.Write(std::string(name) + "/assets/scenes/main.scene", R"({ "version": 1, "entities": [
                { "name": "Floor", "meshRenderer": { "mesh": "meshes/floor.obj", "materials": [ "materials/floor.mat" ] } },
                { "name": "Pyramid", "meshRenderer": { "mesh": "meshes/pyramid.obj" } },
                { "name": "Sun", "light": { "type": "directional" } }
            ] })");
    }

    return *project;
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

    // The built-in shaders are what these projects use, so it is the engine's cache that fills
    std::size_t compiledAfterBoth = 0;

    for (int i = 0; i < 10; ++i)
    {
        CAPTURE(i);

        app.Services().OpenProject(i % 2 == 0 ? withMeshes : empty);
        RunFrames(app);
        REQUIRE(IsShowing<ProjectSessionMode>(app));

        app.Services().CloseProject();
        RunFrames(app);
        REQUIRE(IsShowing<ProjectManagerMode>(app));

        if (i == 0)
            CHECK_FALSE(std::filesystem::is_empty(ImportCache::Engine().Folder() / "shaders"));

        if (i == 1)
            compiledAfterBoth = ImportCache::Stats().Stores;
    }

    // Opening a project again compiles nothing that didn't change
    CHECK(ImportCache::Stats().Stores == compiledAfterBoth);

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
        AssetManager& Assets() { return Context().Session.GetAssets(); }
    };
}

// The panels draw whatever is selected on every frame, so asserts and validation errors in those views show up here
TEST_CASE_METHOD(AssetFixture, "Every kind of asset can be selected", "[editor]")
{
    for (const char* path : { "materials/floor.mat", "textures/picture.bmp", "meshes/floor.obj", "shaders/own.hlsl", "scenes/main.scene",
                              "engine/materials/M_Default.mat", "engine/shaders/S_Lit.hlsl" })
    {
        CAPTURE(path);

        const AssetID id = Assets().FindId(path);
        REQUIRE(id);

        Context().Session.SelectAsset(id);
        RunFrames(App);

        CHECK(Context().Session.GetSelectedAsset() == id);
    }
}

// What it lists is checked by the ListsAsset unit test, as nothing here can click in it
TEST_CASE_METHOD(AssetFixture, "The Assets panel draws with and without the engine's assets", "[editor]")
{
    EditorSettings& settings = App.Services().Settings;

    // A folder of the project's own that is named like the engine's, which the panel warns about
    Temp.Write("Assets/assets/engine/mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl" })");
    Context().Deferred.emplace_back([&]
    {
        Assets().Refresh();
    });
    RunFrames(App);

    CHECK_FALSE(Assets().FindId("engine/mine.mat"));

    for (const bool show : { true, false, true })
    {
        CAPTURE(show);
        settings.SetShowEngineContent(show);
        RunFrames(App);
        CHECK(IsShowing<ProjectSessionMode>(App));
    }

    // The settings are shared by the tests that come after
    settings.SetShowEngineContent(false);
}

TEST_CASE_METHOD(AssetFixture, "Selecting an entity and an asset replace each other", "[editor]")
{
    const Entity entity = Context().Session.GetScene().CreateEntity("Thing");

    Context().Session.SelectEntity(entity);
    Context().Session.SelectAsset(Assets().Find<MaterialAsset>("materials/floor.mat"));
    CHECK_FALSE(Context().Session.GetSelected());

    Context().Session.SelectEntity(entity);
    CHECK_FALSE(Context().Session.GetSelectedAsset());
}

namespace
{
    // Draws something in every frame, before the panels, for as long as it lives
    class FrameDraw
    {
    public:
        explicit FrameDraw(std::function<void()> draw)
            : m_draw(std::move(draw)), m_context(ImGui::GetCurrentContext())
        {
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_NewFramePost;
            hook.Callback = [](ImGuiContext*, ImGuiContextHook* self) { (*static_cast<std::function<void()>*>(self->UserData))(); };
            hook.UserData = &m_draw;
            m_hook = ImGui::AddContextHook(m_context, &hook);
        }

        ~FrameDraw() { ImGui::RemoveContextHook(m_context, m_hook); }

        FrameDraw(const FrameDraw&) = delete;
        FrameDraw& operator=(const FrameDraw&) = delete;
    private:
        std::function<void()> m_draw;
        ImGuiContext* m_context;
        ImGuiID m_hook = 0;
    };
}

// What it picks, and when it closes, is checked by the picker's unit tests. Nothing here can click a field's button, so a
// field is drawn the way the Inspector draws its own, and the picker is opened for it.
TEST_CASE_METHOD(AssetFixture, "The asset picker opens from a field and draws both views", "[editor]")
{
    using crimson::editor::ui::AssetField;
    using crimson::editor::ui::AssetPicker;

    AssetPicker& picker = Context().Picker;
    AssetID texture;
    ImGuiID owner = 0;

    const FrameDraw field([&]
    {
        ImGui::Begin("Picker Field");
        owner = ImGui::GetID("##Asset");
        AssetField("##Asset", Context(), AssetType::Of<TextureAsset>(), texture);
        ImGui::End();
    });

    RunFrames(App, 1);
    REQUIRE(owner);

    picker.Open(owner, AssetType::Of<TextureAsset>(), texture, {});
    RunFrames(App);
    CHECK(picker.IsOpenFor(owner));

    // The grid loads the texture's thumbnail on the frame it is first drawn
    picker.SetView(AssetPicker::View::Grid);
    RunFrames(App);
    CHECK(picker.IsOpenFor(owner));
}

TEST_CASE_METHOD(AssetFixture, "A pinged asset is shown in the Assets panel", "[editor]")
{
    const AssetID selected = Assets().Find<MaterialAsset>("materials/floor.mat");
    Context().Session.SelectAsset(selected);

    Context().PingedAsset = Assets().FindId("textures/picture.bmp");
    REQUIRE(Context().PingedAsset);
    RunFrames(App);

    CHECK_FALSE(Context().PingedAsset);
    CHECK(Context().Session.GetSelectedAsset() == selected);

    // An engine asset is only in the panel while the engine's content is shown, so pinging one turns that on
    EditorSettings& settings = App.Services().Settings;
    settings.SetShowEngineContent(false);

    Context().PingedAsset = Assets().FindId("engine/materials/M_Default.mat");
    RunFrames(App);

    CHECK(settings.GetShowEngineContent());
    CHECK(Context().Session.GetSelectedAsset() == selected);

    // The settings are shared by the tests that come after
    settings.SetShowEngineContent(false);
}

// The Inspector draws components from their registered fields, so a field it can't draw would assert here
TEST_CASE_METHOD(AssetFixture, "Every kind of entity draws in the Inspector", "[editor]")
{
    const ComponentType* health = Assets().Components().RegisterRuntime("health", "Health", {
        FieldInfo{ .Key = "max", .Label = "Max", .Type = FieldType::Int, .Default = int32_t{ 3 } },
        FieldInfo{ .Key = "title", .Label = "Title", .Type = FieldType::String, .Default = std::string("Guard") },
        FieldInfo{ .Key = "tint", .Label = "Tint", .Type = FieldType::Color, .Default = glm::vec3(1.0f) },
        FieldInfo{ .Key = "weapon", .Label = "Weapon", .Type = FieldType::Asset, .Default = AssetID{}, .Asset = AssetType::Of<MeshAsset>() },
        FieldInfo{ .Key = "drops", .Label = "Drops", .Type = FieldType::AssetList, .Default = std::vector<AssetID>{ AssetID{} }, .Asset = AssetType::Of<MaterialAsset>(),
                   .Items = [](const void*, AssetManager&) { return std::vector<AssetListItem>{ { "Drop 0", AssetID{} } }; } },
        FieldInfo{ .Key = "size", .Label = "Size", .Type = FieldType::Vec3, .Default = glm::vec3(1.0f), .UniformToggle = true }
    });
    REQUIRE(health);

    Scene& scene = Context().Session.GetScene();
    std::vector<Entity> entities;

    for (const LightType type : { LightType::Directional, LightType::Point, LightType::Spot })
    {
        const Entity light = scene.CreateEntity("Light");
        light.AddComponent<LightComponent>().Type = type;
        entities.push_back(light);
    }

    for (const CameraProjection projection : { CameraProjection::Perspective, CameraProjection::Orthographic })
    {
        const Entity camera = scene.CreateEntity("Camera");
        camera.AddComponent<CameraComponent>().Projection = projection;
        entities.push_back(camera);
    }

    const Entity guard = scene.CreateEntity("Guard");
    health->Add(guard);
    entities.push_back(guard);

    // What the Create menu makes, with the mesh renderer's slots named after the mesh's
    const ComponentType* renderer = Assets().Components().Find<MeshRendererComponent>();
    REQUIRE(renderer);

    const auto cube = std::ranges::find(renderer->Presets, "Cube", &ComponentPreset::Name);
    REQUIRE(cube != renderer->Presets.end());

    const Entity cubeEntity = scene.CreateEntity("Cube");
    ApplyPreset(*renderer, *cube, cubeEntity, Assets());
    entities.push_back(cubeEntity);

    SceneData withUnknown;
    withUnknown.Roots.push_back(SceneNode{ .Name = "Door", .Components = { SceneComponent{ .Type = "lock", .Raw = R"({ "code": 1234 })" } } });
    InstantiateScene(withUnknown, scene, {}, Assets(), {});
    entities.push_back(scene.GetEntity(scene.GetRoots().back()));

    for (const entt::entity root : scene.GetRoots())
        entities.push_back(scene.GetEntity(root));

    for (const Entity entity : entities)
    {
        CAPTURE(entity.GetComponent<NameComponent>().Name);
        Context().Session.SelectEntity(entity);
        RunFrames(App, 2);
    }

    CHECK(health->Has(guard));
}

TEST_CASE_METHOD(AssetFixture, "The Scene view draws through a scene camera", "[editor]")
{
    Scene& scene = Context().Session.GetScene();

    const Entity camera = scene.CreateEntity("Camera");
    camera.AddComponent<CameraComponent>();
    camera.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, 2.0f, 6.0f);
    Context().Session.SelectEntity(camera);

    auto& view = Session().GetSceneView();
    view.LookThrough(camera);
    REQUIRE(view.IsLookingThrough());
    RunFrames(App);
    CHECK(view.IsLookingThrough());

    camera.GetComponent<CameraComponent>().Projection = CameraProjection::Orthographic;
    RunFrames(App);
    CHECK(view.IsLookingThrough());

    // How the view lets go of a camera is up to its unit tests. This is that the session lets it, before drawing.
    scene.DestroyEntity(camera);
    RunFrames(App);
    CHECK_FALSE(view.IsLookingThrough());
}

// The panels queue these for after the frame, as they can't change GPU resources in the middle of it
TEST_CASE_METHOD(AssetFixture, "Creating from the Hierarchy's presets, and drawing after asset changes", "[editor]")
{
    const ComponentType* renderer = Assets().Components().Find<MeshRendererComponent>();
    REQUIRE(renderer);

    const auto cube = std::ranges::find(renderer->Presets, "Cube", &ComponentPreset::Name);
    REQUIRE(cube != renderer->Presets.end());

    Entity created;
    Context().Deferred.emplace_back([&]
    {
        created = Context().Session.GetScene().CreateEntity("Cube");
        ApplyPreset(*renderer, *cube, created, Assets());
        Context().Session.SelectEntity(created);
    });
    RunFrames(App);

    REQUIRE(created);
    CHECK(Assets().Get(created.GetComponent<MeshRendererComponent>().MeshAsset));

    Context().Deferred.emplace_back([&]
    {
        Assets().Refresh();
    });
    RunFrames(App);

    REQUIRE(created);
    CHECK(Assets().Get(created.GetComponent<MeshRendererComponent>().MeshAsset));
}

TEST_CASE_METHOD(AssetFixture, "A material's edits show, undo, redo and save", "[editor]")
{
    const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");
    const MaterialHandle handle = *Assets().Get(id);

    Context().Session.SelectAsset(id);
    RunFrames(App);

    MaterialData data = *Context().Session.GetAssetEdits().Get(id);
    data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
    data.Textures["ColorMap"] = Assets().Find<TextureAsset>("textures/picture.bmp");
    Context().Session.GetAssetEdits().Set(id, data);
    RunFrames(App);

    CHECK(*Assets().Get(id) == handle);
    CHECK(*Assets().GetData(id) == data);
    CHECK(Context().Session.GetAssetEdits().IsDirty(id));

    Session().GetSession().Undo();
    RunFrames(App);

    CHECK_FALSE(Assets().GetData(id)->Textures.contains("ColorMap"));
    CHECK_FALSE(Context().Session.GetAssetEdits().IsDirty(id));

    Session().GetSession().Redo();
    RunFrames(App);

    CHECK(*Assets().GetData(id) == data);
    CHECK(Context().Session.GetAssetEdits().IsDirty(id));

    REQUIRE(Context().Session.GetAssetEdits().Save(id));
    CHECK(Assets().Read(id)->Properties.at("Color") == data.Properties.at("Color"));

    // A scene step undone with its entity still selected leaves no selection behind for a later step to undo into
    Scene& scene = Context().Session.GetScene();
    const Entity entity = scene.GetEntity(scene.GetRoots()[0]);
    Context().Session.SelectEntity(entity);
    RunFrames(App);

    Context().Session.GetSceneEdits().Record(entity);
    entity.GetComponent<TransformComponent>().Position.x += 1.0f;
    REQUIRE(Session().GetSession().GetHistory().Commit());
    Session().GetSession().Undo();
    Session().GetSession().Redo();

    Context().Session.SelectAsset(id);
    RunFrames(App);

    MaterialData again = *Context().Session.GetAssetEdits().Get(id);
    again.Properties["Color"] = { 0.0f, 0.0f, 1.0f, 1.0f };
    Context().Session.GetAssetEdits().Set(id, again);
    Session().GetSession().Undo();

    CHECK(Context().Session.GetSelectedAsset() == AssetID(id));
    CHECK_FALSE(Context().Session.GetSelected());
}

TEST_CASE_METHOD(AssetFixture, "Changing a material's shader keeps the scene drawing", "[editor]")
{
    const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");
    const MaterialHandle before = *Assets().Get(id);

    MaterialData data = *Context().Session.GetAssetEdits().Get(id);
    data.Shader = Assets().Find<ShaderAsset>("shaders/own.hlsl");
    Context().Session.GetAssetEdits().Set(id, data);
    RunFrames(App);

    CHECK(*Assets().Get(id) != before);

    // The floor uses this material, and it still has a valid handle or a draw would have failed
    CHECK(App.Services().Graphics.Renderer->GetStats().DrawCalls > 0);
}

TEST_CASE_METHOD(AssetFixture, "Reimporting a texture a panel is showing", "[editor]")
{
    const TextureID id = Assets().Find<TextureAsset>("textures/picture.bmp");

    Context().Session.SelectAsset(id);
    RunFrames(App);

    nlohmann::json settings = Assets().GetInfo(id)->Settings;
    settings["flipY"] = true;

    Context().Deferred.emplace_back([&] { Context().Thumbnails.Release(id); Assets().Reimport(id, settings); });
    RunFrames(App);

    CHECK(Assets().GetInfo(id)->Settings.at("flipY") == true);
}

TEST_CASE_METHOD(AssetFixture, "Deleting assets the scene is using", "[editor]")
{
    SECTION("A material: the entity falls back to the default one")
    {
        const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");
        Context().Session.SelectAsset(id);
        RunFrames(App);

        Context().Deferred.emplace_back([&]
        {
            REQUIRE(Assets().Delete(id));
            const std::array<AssetID, 1> removed = { id };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK_FALSE(Context().Session.GetSelectedAsset());
        CHECK(App.Services().Graphics.Renderer->GetStats().DrawCalls > 0);
    }

    SECTION("A mesh: the entity stops drawing it")
    {
        const MeshID id = Assets().Find<MeshAsset>("meshes/floor.obj");

        Context().Deferred.emplace_back([&]
        {
            REQUIRE(Assets().Delete(id));
            const std::array<AssetID, 1> removed = { id };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK(IsShowing<ProjectSessionMode>(App));
    }

    SECTION("A texture a material is showing, and a shader")
    {
        const MaterialID material = Assets().Find<MaterialAsset>("materials/floor.mat");

        MaterialData data = *Context().Session.GetAssetEdits().Get(material);
        data.Textures["ColorMap"] = Assets().Find<TextureAsset>("textures/picture.bmp");
        Context().Session.GetAssetEdits().Set(material, data);
        Context().Session.SelectAsset(material);
        RunFrames(App);

        Context().Deferred.emplace_back([&]
        {
            const TextureID texture = Assets().Find<TextureAsset>("textures/picture.bmp");
            REQUIRE(Assets().Delete(texture));
            const std::array<AssetID, 1> removed = { texture };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        // Kept as a missing one, for it to work again if the file comes back
        const AssetID texture = Assets().GetData(material)->Textures.at("ColorMap");
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

        CHECK_FALSE(Assets().Find<MaterialAsset>("materials/floor.mat"));
    }
}

TEST_CASE_METHOD(AssetFixture, "Closing with unsaved changes asks first", "[editor]")
{
    const MaterialID id = Assets().Find<MaterialAsset>("materials/floor.mat");

    MaterialData data = *Context().Session.GetAssetEdits().Get(id);
    data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
    Context().Session.GetAssetEdits().Set(id, data);
    RunFrames(App);

    App.RequestQuit();
    RunFrames(App);

    CHECK(App.IsRunning());
    REQUIRE(Session().GetSession().IsClosePending());

    SECTION("Cancel stays")
    {
        Session().GetSession().CancelClose();
        RunFrames(App);

        CHECK(App.IsRunning());
        CHECK(Context().Session.GetAssetEdits().IsDirty(id));
    }

    SECTION("Discard closes without saving")
    {
        Session().GetSession().DiscardAllAndClose();

        CHECK_FALSE(App.IsRunning());
        CHECK_FALSE(Assets().Read(id)->Properties.contains("Color"));
    }

    SECTION("Save writes it and closes")
    {
        Session().GetSession().SaveAllAndClose();

        CHECK_FALSE(App.IsRunning());
        CHECK(Assets().Read(id)->Properties.at("Color") == data.Properties.at("Color"));
    }
}

TEST_CASE_METHOD(AssetFixture, "A scene's changes show and can be saved", "[editor]")
{
    const SceneID id = Assets().Find<SceneAsset>("scenes/main.scene");
    REQUIRE(Context().Session.GetSceneAsset() == id);
    CHECK_FALSE(Session().GetSession().CheckSceneChanged());

    Context().Session.GetScene().CreateEntity("New");
    CHECK(Session().GetSession().CheckSceneChanged());

    SECTION("Saving writes it")
    {
        REQUIRE(Session().GetSession().SaveAll());

        CHECK_FALSE(Session().GetSession().CheckSceneChanged());
        CHECK(Assets().Read(id)->Roots.back().Name == "New");
    }

    SECTION("The main camera is part of it")
    {
        const Entity camera = Context().Session.GetScene().CreateEntity("Camera");
        camera.AddComponent<CameraComponent>();
        REQUIRE(Session().GetSession().SaveAll());
        REQUIRE_FALSE(Session().GetSession().CheckSceneChanged());

        const AssetID cameraId = camera.GetComponent<NameComponent>().Id;
        Session().GetSession().GetSettings().MainCamera = cameraId;
        CHECK(Session().GetSession().CheckSceneChanged());

        REQUIRE(Session().GetSession().SaveAll());
        CHECK_FALSE(Session().GetSession().CheckSceneChanged());

        // What a reopened scene is made from
        CHECK(Assets().Read(id)->Settings.MainCamera == cameraId);
    }

    SECTION("Closing asks first")
    {
        App.RequestQuit();
        RunFrames(App);
        REQUIRE(Session().GetSession().IsClosePending());

        Session().GetSession().SaveAllAndClose();

        CHECK_FALSE(App.IsRunning());
        CHECK(Assets().Read(id)->Roots.back().Name == "New");
    }

    SECTION("Opening another scene asks first, and discarding leaves the file")
    {
        SceneID other;
        Context().Deferred.emplace_back([&] { other = Assets().Create<SceneAsset>("scenes/other.scene"); });
        RunFrames(App);
        REQUIRE(other);

        Context().Session.OpenScene(other);
        RunFrames(App);
        REQUIRE(Session().GetSession().IsClosePending());
        CHECK(Context().Session.GetSceneAsset() == id);

        Session().GetSession().DiscardAllAndClose();
        RunFrames(App);

        CHECK(Context().Session.GetSceneAsset() == other);
        CHECK_FALSE(Session().GetSession().CheckSceneChanged());
        CHECK(Assets().Read(id)->Roots.back().Name == "Sun");
    }
}

TEST_CASE_METHOD(AssetFixture, "Scene edits can be undone and redone", "[editor]")
{
    Scene& scene = Context().Session.GetScene();
    REQUIRE(scene.GetRoots().size() >= 2);
    CHECK_FALSE(Session().GetSession().GetHistory().CanUndo());

    const auto root = [&scene](size_t index) { return scene.GetEntity(scene.GetRoots()[index]); };
    const auto idOf = [](Entity entity) { return entity.GetComponent<NameComponent>().Id; };

    const size_t roots = scene.GetRoots().size();
    const AssetID firstId = idOf(root(0));

    SECTION("A deleted entity comes back where it was, with its id, and the scene is as saved again")
    {
        Context().Session.GetSceneEdits().RecordDestroy(root(0));
        scene.DestroyEntity(root(0));
        Session().GetSession().GetHistory().Commit();
        REQUIRE(Session().GetSession().GetHistory().CanUndo());

        Session().GetSession().Undo();

        REQUIRE(scene.GetRoots().size() == roots);
        CHECK(idOf(root(0)) == firstId);
        CHECK_FALSE(Session().GetSession().CheckSceneChanged());
        CHECK_FALSE(Context().Session.IsSceneChanged());

        Session().GetSession().Redo();

        CHECK(scene.GetRoots().size() == roots - 1);
        CHECK(Session().GetSession().CheckSceneChanged());
    }

    SECTION("A deleted entity that was selected is selected again, by its id")
    {
        Context().Session.SelectEntity(root(0));
        RunFrames(App, 1);

        Context().Deferred.emplace_back([&]
        {
            Context().Session.GetSceneEdits().RecordDestroy(root(0));
            scene.DestroyEntity(root(0));
        });
        RunFrames(App, 1);
        CHECK_FALSE(Context().Session.GetSelected());

        Session().GetSession().Undo();

        REQUIRE(Context().Session.GetSelected());
        CHECK(idOf(Context().Session.GetSelected()) == firstId);
        CHECK(Context().Session.GetSelected() == root(0));

        // With nothing left to undo, the selection stays as it is
        REQUIRE_FALSE(Session().GetSession().GetHistory().CanUndo());
        Context().Session.SelectEntity({});
        Session().GetSession().Undo();
        CHECK_FALSE(Context().Session.GetSelected());
    }

    SECTION("The selected entity is recorded, so what the Inspector or the gizmo does to it is one step")
    {
        const Entity selected = root(1);
        Context().Session.SelectEntity(selected);
        RunFrames(App, 1);

        // Changed over a few frames, as a drag would
        for (int frame = 0; frame < 3; ++frame)
            selected.GetComponent<TransformComponent>().Position.x += 1.0f;

        Context().Session.MarkEdited();
        RunFrames(App, 1);

        Session().GetSession().Undo();

        // The same entity, as only it was put back
        CHECK(Context().Session.GetSelected() == selected);
        CHECK(selected.GetComponent<TransformComponent>().Position.x == 0.0f);
        CHECK_FALSE(Session().GetSession().GetHistory().CanUndo());
    }

    SECTION("Edits recorded between frames are each a step of their own, right away")
    {
        Context().Deferred.emplace_back([&] { Context().Session.GetSceneEdits().RecordCreated(scene.CreateEntity("First")); });
        RunFrames(App, 1);
        CHECK(Session().GetSession().GetHistory().CanUndo());

        Context().Deferred.emplace_back([&] { Context().Session.GetSceneEdits().RecordCreated(scene.CreateEntity("Second")); });
        RunFrames(App, 1);

        Session().GetSession().Undo();
        CHECK(scene.GetRoots().size() == roots + 1);

        Session().GetSession().Undo();
        CHECK(scene.GetRoots().size() == roots);
    }

    SECTION("Nothing is undone while the asset picker is open")
    {
        Context().Deferred.emplace_back([&] { Context().Session.GetSceneEdits().RecordCreated(scene.CreateEntity("First")); });
        RunFrames(App, 1);

        Context().Picker.Open(1, AssetType::Of<TextureAsset>(), {}, {});
        RunFrames(App, 1);
        REQUIRE(Context().Picker.IsOpen());

        Session().GetSession().Undo();
        CHECK(scene.GetRoots().size() == roots + 1);

        Context().Picker.Close();
        RunFrames(App, 1);

        Session().GetSession().Undo();
        CHECK(scene.GetRoots().size() == roots);
    }

    SECTION("Opening another scene forgets the steps")
    {
        Context().Session.GetSceneEdits().RecordDestroy(root(0));
        scene.DestroyEntity(root(0));
        Session().GetSession().GetHistory().Commit();
        REQUIRE(Session().GetSession().SaveAll());

        SceneID other;
        Context().Deferred.emplace_back([&] { other = Assets().Create<SceneAsset>("scenes/other.scene"); });
        RunFrames(App);

        Context().Session.OpenScene(other);
        RunFrames(App);

        REQUIRE(Context().Session.GetSceneAsset() == other);
        CHECK_FALSE(Session().GetSession().GetHistory().CanUndo());
        CHECK_FALSE(Session().GetSession().GetHistory().CanRedo());
    }
}

TEST_CASE_METHOD(AssetFixture, "Opening a scene replaces the one showing", "[editor]")
{
    const Entity old = Context().Session.GetScene().GetEntity(Context().Session.GetScene().GetRoots()[0]);
    Context().Session.SelectEntity(old);

    SceneID other;
    Context().Deferred.emplace_back([&] { other = Assets().Create<SceneAsset>("scenes/other.scene"); });
    RunFrames(App);

    Context().Session.OpenScene(other);
    RunFrames(App);

    CHECK(Context().Session.GetSceneAsset() == other);
    CHECK_FALSE(Context().Session.GetSelected());
    REQUIRE(Context().Session.GetScene().GetRoots().size() == 1);
    CHECK(Context().Session.GetScene().GetEntity(Context().Session.GetScene().GetRoots()[0]).GetComponent<NameComponent>().Name == "Sun");

    SECTION("It is the one that opens next time")
    {
        App.Services().CloseProject();
        RunFrames(App);
        App.Services().OpenProject(OpenProject);
        RunFrames(App);

        CHECK(Context().Session.GetSceneAsset() == other);
    }

    SECTION("Deleting it goes on to another one, without asking")
    {
        Context().Session.GetScene().CreateEntity("Unsaved");

        Context().Deferred.emplace_back([&]
        {
            REQUIRE(Assets().Delete(other));
            const std::array<AssetID, 1> removed = { other };
            Context().OnAssetsRemoved(removed);
        });
        RunFrames(App);

        CHECK(Context().Session.GetSceneAsset() == Assets().Find<SceneAsset>("scenes/main.scene"));
        CHECK_FALSE(Session().GetSession().IsClosePending());
    }
}

TEST_CASE_METHOD(AssetFixture, "Deleting the only scene makes a new one", "[editor]")
{
    const SceneID id = Assets().Find<SceneAsset>("scenes/main.scene");

    Context().Deferred.emplace_back([&]
    {
        REQUIRE(Assets().Delete(id));
        const std::array<AssetID, 1> removed = { id };
        Context().OnAssetsRemoved(removed);
    });
    RunFrames(App);

    CHECK(Context().Session.GetSceneAsset());
    CHECK(Context().Session.GetSceneAsset() != id);
    CHECK(IsShowing<ProjectSessionMode>(App));
}

TEST_CASE_METHOD(AssetFixture, "A scene's instances draw, and go empty without their source", "[editor]")
{
    SceneID source;
    Entity instance;

    Temp.Write("Assets/assets/scenes/crate.scene", R"({ "version": 1, "entities": [ { "name": "Crate", "meshRenderer": { "mesh": "meshes/floor.obj" } } ] })");

    Context().Deferred.emplace_back([&]
    {
        Assets().Refresh();
        source = Assets().Find<SceneAsset>("scenes/crate.scene");
        instance = CreateSceneInstance(source, Context().Session.GetScene(), {}, Assets(), Context().Session.GetSceneAsset());
    });
    RunFrames(App);

    REQUIRE(instance);
    REQUIRE(instance.GetComponent<RelationshipComponent>().Children.size() == 1);

    const uint32_t drawCalls = App.Services().Graphics.Renderer->GetStats().DrawCalls;
    CHECK(drawCalls > 0);

    // Panels draw what is selected, read-only or not
    const Entity child = Context().Session.GetScene().GetEntity(instance.GetComponent<RelationshipComponent>().Children[0]);
    Context().Session.SelectEntity(child);
    RunFrames(App);
    Context().Session.SelectEntity(instance);
    RunFrames(App);

    Context().Deferred.emplace_back([&]
    {
        REQUIRE(Assets().Delete(source));
        const std::array<AssetID, 1> removed = { source };
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

    CHECK(session.GetContext().Session.GetSceneAsset() == session.GetContext().Session.GetAssets().Find<SceneAsset>("scenes/main.scene"));
    CHECK(std::filesystem::exists(temp.Path() / "Empty/assets/scenes/main.scene"));
    CHECK_FALSE(session.GetSession().CheckSceneChanged());
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

    CHECK_FALSE(context.Session.GetSceneAsset());
    CHECK(ExpectLoggedErrors("scenes/main.scene") > 0);

    context.Session.GetScene().CreateEntity("Would be lost");
    CHECK_FALSE(session.GetSession().CheckSceneChanged());
    CHECK(session.GetSession().SaveAll());

    std::ifstream stream(file, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()) == kBroken);

    RunFrames(app);
}

TEST_CASE("A project with a broken shader and a broken scene still opens", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Broken", true);
    temp.Write("Broken/assets/broken.hlsl", "this does not compile");
    temp.Write("Broken/assets/materials/floor.mat", R"({ "shader": "broken.hlsl" })");
    temp.Write("Broken/assets/scenes/main.scene", "{ not json");

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    CHECK(IsShowing<ProjectSessionMode>(app));

    CHECK(ExpectLoggedErrors("broken.hlsl") > 0);
    CHECK(ExpectLoggedErrors("materials/floor.mat") > 0);
    CHECK(ExpectLoggedErrors("scenes/main.scene") > 0);
}

static std::string ReadBytes(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

static bool HasEntityNamed(Scene& scene, std::string_view name)
{
    for (auto [handle, entityName] : scene.GetRegistry().view<NameComponent>().each())
    {
        if (entityName.Name == name)
            return true;
    }

    return false;
}

TEST_CASE("A scene that wasn't closed normally is offered again", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Crashed", true);
    const std::filesystem::path sceneFile = project.AssetsDir() / "scenes/main.scene";

    SceneID id;
    {
        EditorApplication app(SelectedBackend(), project.Root());
        RunFrames(app);

        auto& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode());
        id = session.GetContext().Session.GetSceneAsset();
        REQUIRE(id);

        session.GetContext().Session.GetScene().CreateEntity("Unsaved");
        session.GetSession().WriteRecovery();
        CHECK_FALSE(session.GetSession().GetOfferedRecovery().has_value());

        // Gone without closing, like a killed process. The recovery files finish what they were writing.
    }

    const std::filesystem::path copy = project.DataDir() / "recovery" / (id.ToString() + ".scene");
    REQUIRE(std::filesystem::exists(copy));
    const std::string savedBytes = ReadBytes(sceneFile);

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    auto& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode());
    EditorContext& context = session.GetContext();

    REQUIRE(session.GetSession().GetOfferedRecovery().has_value());
    CHECK(context.Session.GetSceneAsset() == id);
    CHECK(context.Session.GetAssets().List(AssetType::Of<SceneAsset>()).size() == 2); // this one and the engine's
    CHECK(std::ranges::count_if(context.Session.GetAssets().List(AssetType::Of<SceneAsset>()), [](const AssetInfo* scene) { return !scene->ReadOnly; }) == 1);
    CHECK_FALSE(session.GetSession().CheckSceneChanged());

    session.GetSession().RestoreRecovery();
    RunFrames(app);

    CHECK_FALSE(session.GetSession().GetOfferedRecovery().has_value());
    CHECK(session.GetSession().CheckSceneChanged());
    CHECK(HasEntityNamed(context.Session.GetScene(), "Unsaved"));
    CHECK(ReadBytes(sceneFile) == savedBytes);

    REQUIRE(session.GetSession().SaveAll());
    CHECK(context.Session.GetAssets().Read(id)->Roots.back().Name == "Unsaved");
    CHECK_FALSE(std::filesystem::exists(copy));
}

TEST_CASE("Discarding a recovery copy keeps the file, and a stale one goes without asking", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Recovery", true);
    const std::filesystem::path mainFile = project.AssetsDir() / "scenes/main.scene";

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    auto& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode());
    EditorContext& context = session.GetContext();
    const SceneID main = context.Session.GetSceneAsset();
    REQUIRE(main);

    SceneID other;
    context.Deferred.emplace_back([&] { other = context.Session.GetAssets().Create<SceneAsset>("scenes/other.scene"); });
    RunFrames(app);
    REQUIRE(other);

    const std::filesystem::path otherFile = project.AssetsDir() / "scenes/other.scene";
    const std::string otherBytes = ReadBytes(otherFile);

    // An hour newer than its file, as two writes close together can get the same time
    const std::filesystem::path otherCopy = temp.Write("Recovery/.crimson/recovery/" + other.ToString() + ".scene",
                                                       R"({ "version": 1, "entities": [ { "name": "From the copy" } ] })");
    std::filesystem::last_write_time(otherCopy, std::filesystem::last_write_time(otherFile) + std::chrono::hours(1));

    // An hour older than its file
    const std::filesystem::path mainCopy = temp.Write("Recovery/.crimson/recovery/" + main.ToString() + ".scene",
                                                      R"({ "version": 1, "entities": [ { "name": "Too old" } ] })");
    std::filesystem::last_write_time(mainCopy, std::filesystem::last_write_time(mainFile) - std::chrono::hours(1));

    context.Session.OpenScene(other);
    RunFrames(app);

    REQUIRE(context.Session.GetSceneAsset() == other);
    REQUIRE(session.GetSession().GetOfferedRecovery().has_value());

    session.GetSession().DiscardRecovery();
    CHECK_FALSE(session.GetSession().GetOfferedRecovery().has_value());
    CHECK_FALSE(std::filesystem::exists(otherCopy));
    CHECK_FALSE(session.GetSession().CheckSceneChanged());
    CHECK_FALSE(HasEntityNamed(context.Session.GetScene(), "From the copy"));
    CHECK(ReadBytes(otherFile) == otherBytes);

    context.Session.OpenScene(main);
    RunFrames(app);

    CHECK(context.Session.GetSceneAsset() == main);
    CHECK_FALSE(session.GetSession().GetOfferedRecovery().has_value());
    CHECK_FALSE(std::filesystem::exists(mainCopy));

    app.RequestQuit();
    CHECK_FALSE(app.IsRunning());
}
