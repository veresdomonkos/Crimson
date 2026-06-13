#pragma once

#include "crimson/core/window.hpp"

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
		bool m_running;
	};
}
