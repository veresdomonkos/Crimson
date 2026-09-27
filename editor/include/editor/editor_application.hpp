#pragma once

#include <crimson/core/window.hpp>
#include <crimson/renderer/renderer.hpp>
#include <crimson/ui/imgui_backend.hpp>

#include "editor_ui.hpp"

namespace crimson::editor
{
	class EditorApplication
	{
	public:
		EditorApplication();
        ~EditorApplication();
		void Run();
	    void OnEvent(Event& event);
	    void HandleMove(float deltaTime);
	private:
		std::unique_ptr<Window> m_window;
	    RenderSurfaceHandle m_primarySurface;
	    std::unique_ptr<Renderer> m_renderer;
	    std::unique_ptr<ImGuiBackend> m_imguiBackend;
	    glm::vec3 m_cameraPosition{};
	    PerspectiveCamera m_camera{};
	    EditorUI m_ui;
		bool m_running;
	    double m_lastTime;
	};
}
