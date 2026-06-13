#include "crimson/core/application.hpp"

#include "crimson/core/core.hpp"
#include "crimson/renderer/renderer.hpp"

namespace crimson
{
	Application::Application() : m_running(true)
	{
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
	    Renderer::Initialize();
	}

    Application::~Application()
    {
	    Renderer::Shutdown();
    }

    void Application::Run()
	{
	    m_window->MakeCurrent();

		while (m_running)
		{
            m_window->PollEvents();

		    Renderer::BeginScene();
		    Renderer::Clear(glm::vec4(0, 0, 1, 1));
		    Renderer::EndScene();

		    m_window->SwapBuffers();
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
