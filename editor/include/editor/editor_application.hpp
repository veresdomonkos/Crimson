#pragma once

#include <crimson/core/window.hpp>
#include <crimson/renderer/renderer.hpp>

namespace crimson::editor
{
	class EditorApplication
	{
	public:
		EditorApplication();
        ~EditorApplication();
		void Run();
	    void OnEvent(Event& event);
	private:
		std::unique_ptr<Window> m_window;
	    RenderSurfaceHandle m_primarySurface;
	    std::unique_ptr<Renderer> m_renderer;
		bool m_running;
	};
}
