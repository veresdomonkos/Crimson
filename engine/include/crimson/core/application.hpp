#pragma once

#include "crimson/core/window.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
	class Application
	{
	public:
		Application();
        ~Application();
		void Run();
		void OnEvent(Event& event);
	private:
		std::unique_ptr<Window> m_window;
	    RenderSurfaceHandle m_primarySurface;
	    std::unique_ptr<Renderer> m_renderer;
		bool m_running;
	};
}
