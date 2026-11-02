#include "crimson_editor/editor_application.hpp"

#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/core/profiler.hpp>
#include <crimson/project/project.hpp>
#include <crimson/renderer/renderer_api.hpp>
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <utility>

#include "crimson_editor/editor_paths.hpp"
#include "crimson_editor/project_manager_mode.hpp"
#include "crimson_editor/project_session_mode.hpp"
#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor
{
    static constexpr uint32_t kProjectManagerWidth = 1000;
    static constexpr uint32_t kProjectManagerHeight = 620;
    static constexpr uint32_t kEditorWidth = 1280;
    static constexpr uint32_t kEditorHeight = 720;

	EditorApplication::EditorApplication(RendererAPIType rendererType, const std::filesystem::path& projectPath)
        : m_running(true), m_lastTime(0.0)
    {
        if (NFD_Init() != NFD_OKAY)
            LOG_ERROR("[Editor] The folder picker is unavailable: {}", NFD_GetError());

        const std::filesystem::path configFolder = GetUserConfigFolder();
        m_imguiIniPath = (configFolder / "imgui.ini").string();

        m_window          = Window::Create(rendererType, WindowData{ "Crimson", kProjectManagerWidth, kProjectManagerHeight, BIND_FN(OnEvent) });
        m_graphicsBackend = GraphicsBackend::Create(rendererType, *m_window);
        m_input           = std::make_unique<Input>(*m_window);
        m_icons           = std::make_unique<EditorIcons>(*m_graphicsBackend->GPUResources, *m_graphicsBackend->Imgui);
        m_recentProjects  = std::make_unique<RecentProjects>(configFolder / "recent_projects.json");

        m_services = std::make_unique<EditorServices>(EditorServices{
            .AppWindow = *m_window,
            .Graphics = *m_graphicsBackend,
            .AppInput = *m_input,
            .RendererType = rendererType,
            .Icons = *m_icons,
            .Recents = *m_recentProjects,
            .OpenProject = [this](Project project) { m_requestedProject.emplace(std::move(project)); },
            .CloseProject = [this] { m_closeProjectRequested = true; }
        });

        ImGui::GetIO().IniFilename = m_imguiIniPath.c_str();
        ui::ApplyEditorTheme();

        m_lastTime = glfwGetTime();

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
        m_window->SetSize(kProjectManagerWidth, kProjectManagerHeight);
        m_mode = std::make_unique<ProjectManagerMode>(*m_services, std::move(error));
    }

    void EditorApplication::ShowProject(Project project)
    {
        // The old mode's GPU resources go first, so two projects are never loaded at once
        m_mode.reset();

        m_recentProjects->Add(project);

        m_window->SetTitle(project.Name() + " - Crimson Editor");
        m_window->SetSize(kEditorWidth, kEditorHeight);
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
        m_lastTime = glfwGetTime();
    }

    void EditorApplication::Run()
    {
        while (m_running)
            RunFrame();
    }

    void EditorApplication::RunFrame()
    {
        const double currentTime = glfwGetTime();
        const auto deltaTime = static_cast<float>(currentTime - m_lastTime);
        m_lastTime = currentTime;

        {
            CRIMSON_PROFILE_SCOPE("Poll Events");
            m_window->PollEvents();
        }
        RenderFrame();

        m_input->Update();
        m_mode->Update(deltaTime);

        ApplyRequestedMode();

        CRIMSON_PROFILE_FRAME();
    }

    void EditorApplication::RenderFrame()
    {
        CRIMSON_PROFILE_FUNCTION();

        auto& renderer = *m_graphicsBackend->Renderer;

        m_mode->PrepareFrame();

        auto frame = renderer.BeginFrame(m_mode->Lighting());

        if (!frame.ShouldRender())
            return;

	    m_graphicsBackend->Imgui->NewFrame();

        m_mode->RecordPasses(frame);
        RecordUIPass(frame);

        renderer.EndFrame(frame);
    }

    void EditorApplication::RecordUIPass(FrameContext& frame)
    {
        RawPassInfo info{
            .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(0.09f, 0.10f, 0.11f, 1.0f),
            .Callback = [this](const NativeFrameHandles& handles)
            {
                m_mode->OnImGui();

                ImGui::Render();
                m_graphicsBackend->Imgui->RenderDrawData(ImGui::GetDrawData(), handles);
            }
        };
        frame.AddRawPass(info);
    }

	void EditorApplication::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			m_running = false;
			return true;
		});
	}
}
