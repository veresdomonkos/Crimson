#include "crimson/core/application.hpp"

#include "crimson/core/core.hpp"

namespace crimson
{
	Application::Application() : m_running(true)
	{
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
	    m_renderer = Renderer::Create();
	    m_primarySurface = m_renderer->Initialize(*m_window);
	}

    Application::~Application()
    {

    }

    void Application::Run()
	{
		while (m_running)
		{
		    m_window->PollEvents();

            Frame frame = m_renderer->BeginFrame(m_primarySurface);
		    frame.Clear({0.8, 0.2, 0.3, 1});
		    m_renderer->EndFrame(frame);
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
