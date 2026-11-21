#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <crimson/core/input.hpp>
#include <crimson/core/window.hpp>
#include <crimson/graphics/graphics_backend.hpp>
#include <crimson/project/project.hpp>

#include "crimson_editor/app_mode.hpp"
#include "crimson_editor/editor_icons.hpp"
#include "crimson_editor/editor_settings.hpp"
#include "crimson_editor/recent_projects.hpp"
#include "crimson_editor/ui/host.hpp"

namespace crimson::editor
{
	// What lives for the whole run: the window, the graphics backend and the mode being shown
	class EditorApplication
	{
	public:
		// An empty path starts with the project manager
		EditorApplication(GraphicsApi rendererType, const std::filesystem::path& projectPath);
        ~EditorApplication();
		void Run();

		// One step of Run, for driving the editor from outside
		void RunFrame();
		[[nodiscard]] EditorServices& Services() { return *m_services; }
		[[nodiscard]] AppMode& CurrentMode() { return *m_mode; }
		[[nodiscard]] const AppMode& CurrentMode() const { return *m_mode; }
		[[nodiscard]] bool IsRunning() const { return m_running; }

		// The same as the window's close button
		void RequestQuit() { m_mode->RequestClose([this] { m_running = false; }); }
	private:
	    // --- Frame ---
        void RenderFrame(float deltaTime);

	    // --- Modes ---
	    void ShowProjectManager(std::string error = {});
	    void ShowProject(Project project);
	    void ApplyRequestedMode();

	    // --- Input / events ---
	    void OnEvent(Event& event);
	private:
	    bool m_running;
        std::chrono::steady_clock::time_point m_lastTime;

        std::unique_ptr<Window>          m_window;
        std::unique_ptr<GraphicsBackend> m_graphicsBackend;

        // After the backend, so what the UI made on the GPU is destroyed while it is there
        std::unique_ptr<ui::Host>        m_ui;

	    std::unique_ptr<Input>           m_input;
	    std::unique_ptr<EditorIcons>     m_icons;
	    std::unique_ptr<RecentProjects>  m_recentProjects;
	    std::unique_ptr<EditorSettings>  m_settings;
	    std::unique_ptr<EditorServices>  m_services;

	    std::unique_ptr<AppMode> m_mode;

	    std::optional<Project> m_requestedProject;
	    bool m_closeProjectRequested = false;
	};
}
