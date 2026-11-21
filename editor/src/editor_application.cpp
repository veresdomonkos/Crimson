#include "crimson_editor/editor_application.hpp"

#include <crimson/asset_manager/import_cache.hpp>
#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/core/profiler.hpp>
#include <crimson/project/project.hpp>
#include <crimson/graphics/graphics_api.hpp>
#include <nfd.h>
#include <chrono>
#include <exception>
#include <utility>

#include "crimson_editor/editor_paths.hpp"
#include "crimson_editor/project_manager_mode.hpp"
#include "crimson_editor/project_session_mode.hpp"
#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor
{
    constexpr uint32_t kProjectManagerWidth = 1000;
    constexpr uint32_t kProjectManagerHeight = 620;
    constexpr uint32_t kEditorWidth = 1280;
    constexpr uint32_t kEditorHeight = 720;

	EditorApplication::EditorApplication(GraphicsApi rendererType, const std::filesystem::path& projectPath)
        : m_running(true), m_lastTime(std::chrono::steady_clock::now())
    {
        if (NFD_Init() != NFD_OKAY)
            LOG_ERROR("[Editor] The folder picker is unavailable: {}", NFD_GetError());

        ImportCache::Engine().SetFolder(paths::EngineCacheFolder());

        const std::filesystem::path configFolder = paths::UserConfigFolder();
        m_imguiIniPath = (configFolder / "imgui.ini").string();

        // The window is made for its API, as OpenGL needs a context and Vulkan has none, so a fallback makes a new one
        const auto createGraphics = [this](GraphicsApi api)
        {
            m_graphicsBackend.reset();
            m_window          = Window::Create(api, WindowData{ "Crimson", kProjectManagerWidth, kProjectManagerHeight, BIND_FN(OnEvent) });
            m_graphicsBackend = m_window->CreateGraphicsBackend();
        };

        try
        {
            createGraphics(rendererType);
        }
        catch (const std::exception& error)
        {
            if (rendererType != GraphicsApi::Vulkan)
                throw;

            LOG_ERROR("[Editor] Vulkan couldn't start ({}), using OpenGL", error.what());
            rendererType = GraphicsApi::OpenGL;
            createGraphics(rendererType);
        }

        // After the graphics backend is final, as the fallback above makes a new window and backend
        m_imguiPlatform   = std::make_unique<ui::ImGuiPlatform>(*m_window);
        m_imguiRenderer   = std::make_unique<ui::ImGuiRenderer>(*m_graphicsBackend->GPUResources);

        m_input           = std::make_unique<Input>();
        m_icons           = std::make_unique<EditorIcons>(*m_graphicsBackend->GPUResources);
        m_recentProjects  = std::make_unique<RecentProjects>(configFolder / "recent_projects.json");
        m_settings        = std::make_unique<EditorSettings>(configFolder / "settings.json");

        m_services = std::make_unique<EditorServices>(EditorServices{
            .AppWindow = *m_window,
            .Graphics = *m_graphicsBackend,
            .AppInput = *m_input,
            .RendererType = rendererType,
            .Icons = *m_icons,
            .Recents = *m_recentProjects,
            .Settings = *m_settings,
            .OpenProject = [this](Project project) { m_requestedProject.emplace(std::move(project)); },
            .CloseProject = [this] { m_closeProjectRequested = true; }
        });

        ImGui::GetIO().IniFilename = m_imguiIniPath.c_str();
        ui::theme::Apply(m_window->GetContentScale());

        m_lastTime = std::chrono::steady_clock::now();

        if (projectPath.empty())
        {
            ShowProjectManager();
            return;
        }

        if (auto project = Project::Load(projectPath))
            ShowProject(std::move(*project));
        else
            ShowProjectManager(std::move(project.error()));
    }

    EditorApplication::ImGuiContextOwner::ImGuiContextOwner()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }

    EditorApplication::ImGuiContextOwner::~ImGuiContextOwner()
    {
        ImGui::DestroyContext();
    }

    EditorApplication::~EditorApplication()
    {
        // Before the backend is gone
        m_mode.reset();

        NFD_Quit();
    }

    void EditorApplication::ShowProjectManager(std::string error)
    {
        m_mode.reset();

        if (!error.empty())
            LOG_ERROR("[Editor] {}", error);

        m_window->SetTitle("Crimson - Projects");
        m_window->Restore();
        m_window->SetSize(kProjectManagerWidth, kProjectManagerHeight);
        m_mode = std::make_unique<ProjectManagerMode>(*m_services, std::move(error));
    }

    void EditorApplication::ShowProject(Project project)
    {
        // The old mode's GPU resources go first, so two projects are never loaded at once
        m_mode.reset();

        m_recentProjects->Add(project);

        m_window->SetTitle(project.Name() + " - Crimson Editor");

        // The size is what it goes back to when no longer maximized
        m_window->SetSize(kEditorWidth, kEditorHeight);
        m_window->Maximize();

        m_mode = std::make_unique<ProjectSessionMode>(*m_services, std::move(project));
    }

    void EditorApplication::ApplyRequestedMode()
    {
        if (m_requestedProject)
        {
            Project project = std::move(*m_requestedProject);
            m_requestedProject.reset();
            ShowProject(std::move(project));
        }
        else if (m_closeProjectRequested)
        {
            ShowProjectManager();
        }
        else
        {
            return;
        }

        m_closeProjectRequested = false;

        // Loading isn't part of any frame, so the camera shouldn't see it as one
        m_lastTime = std::chrono::steady_clock::now();
    }

    void EditorApplication::Run()
    {
        while (m_running)
            RunFrame();
    }

    void EditorApplication::RunFrame()
    {
        const auto currentTime = std::chrono::steady_clock::now();
        const auto deltaTime = std::chrono::duration<float>(currentTime - m_lastTime).count();
        m_lastTime = currentTime;

        {
            CRIMSON_PROFILE_SCOPE("Poll Events");
            m_window->PollEvents();
        }
        RenderFrame(deltaTime);

        m_input->Update();
        m_mode->Update(deltaTime);

        ApplyRequestedMode();

        CRIMSON_PROFILE_FRAME();
    }

    void EditorApplication::RenderFrame(float deltaTime)
    {
        CRIMSON_PROFILE_FUNCTION();

        auto& renderer = *m_graphicsBackend->Renderer;

        if (!renderer.CanRender())
            return;

        m_imguiPlatform->NewFrame(deltaTime);
        ImGui::NewFrame();

        FrameRecorder frame = renderer.BeginRecording();
        m_mode->Record(frame);

        // After the passes, so the UI sees the world transforms they updated
        m_mode->OnImGui();
        ImGui::Render();
        RecordUIPass(frame);

        renderer.Submit(std::move(frame));
    }

    void EditorApplication::RecordUIPass(FrameRecorder& frame)
    {
        // Everything the draw data holds is copied while recording
        m_imguiRenderer->Record(frame, *ImGui::GetDrawData(), PassInfo{
            .Name = "UI",
            .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = glm::vec4(ui::theme::AppBackground.x, ui::theme::AppBackground.y, ui::theme::AppBackground.z, 1.0f) }
        });
    }

	void EditorApplication::OnEvent(Event& event)
	{
        m_input->OnEvent(event);
        m_imguiPlatform->OnEvent(event);

		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			RequestQuit();
			return true;
		});
	}
}
