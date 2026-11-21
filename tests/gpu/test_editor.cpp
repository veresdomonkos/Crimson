#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
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
#include <crimson_editor/ui/window.hpp>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;
using crimson::editor::EditorApplication;
using crimson::editor::EditorContext;
using crimson::editor::EditorSettings;
using crimson::editor::ProjectManagerMode;
using crimson::editor::ProjectSession;
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

    // Every panel that can be closed draws again after it was shown again
    ProjectSessionMode& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode());

    for (const auto& panel : session.GetWorkspace().Panels())
    {
        if (panel->Info().Closable)
            panel->Close();
    }

    RunFrames(app);

    for (const auto& panel : session.GetWorkspace().Panels())
    {
        CAPTURE(panel->Info().Title);
        CHECK(panel->IsOpen() == !panel->Info().Closable);

        panel->Show();
    }

    RunFrames(app);

    for (const auto& panel : session.GetWorkspace().Panels())
    {
        CAPTURE(panel->Info().Title);
        CHECK(panel->IsOpen());
    }
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
    crimson::editor::ui::WidgetId owner = 0;

    const FrameDraw field([&]
    {
        const crimson::editor::ui::WindowScope window("Picker Field");
        owner = crimson::editor::ui::IdOf("##Asset");
        AssetField("##Asset", Context(), AssetType::Of<TextureAsset>(), texture);
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

    // It opens the panel if it was closed
    const auto assetsPanel = std::ranges::find_if(Session().GetWorkspace().Panels(), [](const auto& panel) { return panel->Info().Title == "Assets"; });
    REQUIRE(assetsPanel != Session().GetWorkspace().Panels().end());
    (*assetsPanel)->Close();

    const AssetID picture = Assets().FindId("textures/picture.bmp");
    REQUIRE(picture);
    Context().ShowAsset(picture);
    RunFrames(App);

    CHECK((*assetsPanel)->IsOpen());
    CHECK(Context().Session.GetSelectedAsset() == selected);

    // An engine asset is only in the panel while the engine's content is shown, so pinging one turns that on
    EditorSettings& settings = App.Services().Settings;
    settings.SetShowEngineContent(false);

    const AssetID engineMaterial = Assets().FindId("engine/materials/M_Default.mat");
    REQUIRE(engineMaterial);
    Context().ShowAsset(engineMaterial);
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

    // An instance, and what comes from its scene, which can't be changed
    Temp.Write("Assets/assets/scenes/crate.scene", R"({ "version": 1, "entities": [ { "name": "Crate", "meshRenderer": { "mesh": "meshes/floor.obj" } } ] })");

    Entity instance;
    Context().Deferred.emplace_back([&]
    {
        Assets().Refresh();
        instance = CreateSceneInstance(Assets().Find<SceneAsset>("scenes/crate.scene"), scene, {}, Assets(), Context().Session.GetSceneAsset());
    });
    RunFrames(App);

    REQUIRE(instance);
    REQUIRE(instance.GetComponent<RelationshipComponent>().Children.size() == 1);
    entities.push_back(instance);
    entities.push_back(scene.GetEntity(instance.GetComponent<RelationshipComponent>().Children[0]));

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

TEST_CASE_METHOD(AssetFixture, "Deleting a material and a mesh the scene draws keeps it drawing", "[editor]")
{
    // The entity falls back to the default material
    const MaterialID material = Assets().Find<MaterialAsset>("materials/floor.mat");
    Context().Session.SelectAsset(material);
    RunFrames(App);

    Context().Deferred.emplace_back([&]
    {
        REQUIRE(Assets().Delete(material));
        const std::array<AssetID, 1> removed = { material };
        Context().OnAssetsRemoved(removed);
    });
    RunFrames(App);

    CHECK_FALSE(Context().Session.GetSelectedAsset());
    CHECK(App.Services().Graphics.Renderer->GetStats().DrawCalls > 0);

    // The entity stops drawing its mesh
    const MeshID mesh = Assets().Find<MeshAsset>("meshes/floor.obj");

    Context().Deferred.emplace_back([&]
    {
        REQUIRE(Assets().Delete(mesh));
        const std::array<AssetID, 1> removed = { mesh };
        Context().OnAssetsRemoved(removed);
    });
    RunFrames(App);

    CHECK(IsShowing<ProjectSessionMode>(App));
}

// Separate from the broken scene, as a scene that doesn't load never gets to the shader
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

TEST_CASE("A project with a broken scene still opens", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Broken", true);
    temp.Write("Broken/assets/scenes/main.scene", "{ not json");

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    CHECK(IsShowing<ProjectSessionMode>(app));

    CHECK(ExpectLoggedErrors("scenes/main.scene") > 0);
}

TEST_CASE("A scene that wasn't closed normally is offered again", "[editor]")
{
    ShutdownGpu();

    const TempFolder temp;
    const Project project = MakeProject(temp, "Crashed", true);
    const std::filesystem::path copy = project.DataDir() / "recovery";

    SceneID id;
    {
        EditorApplication app(SelectedBackend(), project.Root());
        RunFrames(app);

        ProjectSession& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode()).GetSession();
        id = session.GetSceneAsset();
        REQUIRE(id);

        session.GetScene().CreateEntity("Unsaved");
        session.WriteRecovery();

        // Gone without closing, like a killed process. The recovery files finish what they were writing.
    }

    REQUIRE(std::filesystem::exists(copy / (id.ToString() + ".scene")));

    EditorApplication app(SelectedBackend(), project.Root());
    RunFrames(app);

    ProjectSession& session = dynamic_cast<ProjectSessionMode&>(app.CurrentMode()).GetSession();
    REQUIRE(session.GetOfferedRecovery());

    session.RestoreRecovery();
    RunFrames(app);

    CHECK_FALSE(session.GetOfferedRecovery());
    CHECK(session.IsSceneChanged());

    // The restored scene is unsaved, so closing asks
    app.RequestQuit();
    RunFrames(app);

    CHECK(app.IsRunning());
    REQUIRE(session.IsClosePending());

    REQUIRE(session.SaveAllAndClose());
    CHECK_FALSE(app.IsRunning());
}
