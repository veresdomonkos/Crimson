#include <catch2/catch_test_macros.hpp>

#include <string>

#include <crimson/core/log.hpp>
#include <crimson/project/project.hpp>
#include <crimson_editor/editor_application.hpp>
#include <crimson_editor/project_manager_mode.hpp>
#include <crimson_editor/project_session_mode.hpp>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using namespace crimson::tests;
using crimson::editor::EditorApplication;
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

    // A project with the meshes the baked scene looks for, and an empty one otherwise
    Project MakeProject(const TempFolder& temp, const char* name, bool withMeshes)
    {
        const auto project = Project::Create(temp.Path(), name);
        REQUIRE(project);

        if (withMeshes)
        {
            temp.Write(std::string(name) + "/assets/meshes/floor.obj", kQuad);
            temp.Write(std::string(name) + "/assets/meshes/pyramid.obj", kQuad);
            temp.Write(std::string(name) + "/assets/materials/floor.mat", R"({ "shader": "engine/shaders/main.hlsl" })");
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
