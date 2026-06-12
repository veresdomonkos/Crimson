#pragma once

#include "crimson/core/window.hpp"
#include "crimson/renderer/renderer_backend.hpp"

namespace crimson
{
	class Application
	{
	public:
		Application();

		void Run();
		void OnEvent(Event& event);
	private:
		std::unique_ptr<Window> m_window;
	    std::unique_ptr<RendererBackend> m_rendererBackend;
		bool m_running;
	};
}
