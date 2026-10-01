#pragma once

#include <crimson/core/window.hpp>
#include <crimson/renderer/renderer.hpp>
#include <crimson/ui/imgui_backend.hpp>

#include "editor_ui.hpp"
#include "crimson/graphics/graphincs_backend.hpp"

namespace crimson::editor
{
	struct FrameStats
	{
		float FPS = 0.0f;
		float FrameTimeMs = 0.0f;
		float RenderMs = 0.0f;

		float FpsAccumulator = 0.0f;
		uint32_t FrameCount = 0;
		float UpdateTimer = 0.0f;
	};

	class EditorApplication
	{
	public:
		EditorApplication();
        ~EditorApplication() = default;
		void Run();
	    void OnEvent(Event& event);
	    void HandleMove(float deltaTime);
	private:
		std::unique_ptr<Window> m_window;
	    std::unique_ptr<GraphicsBackend> m_graphicsBackend;

	    glm::vec3 m_cameraPosition{};
	    PerspectiveCamera m_camera{};
	    EditorUI m_ui;
		bool m_running;
	    double m_lastTime;

		FrameStats m_frameStats;
	};
}
