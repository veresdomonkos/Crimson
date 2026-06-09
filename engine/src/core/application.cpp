#include "crimson/core/application.hpp"

#include "crimson/core/core.hpp"

namespace crimson
{
	Application::Application() : m_running(true)
	{
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
	}

	void Application::Run()
	{
		while (m_running)
		{
			m_window->SwapBuffers();
			m_window->PollEvents();
		}
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			m_running = false;
			return true;
		});
	}
}