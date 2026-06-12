#include "crimson/core/application.hpp"

#include "crimson/core/core.hpp"

namespace crimson
{
	Application::Application() : m_running(true)
	{
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
        m_rendererBackend = RendererBackend::Create();
	    m_rendererBackend->Initialize(m_window->GetNativeHandle());
	}

	void Application::Run()
	{
	    RenderCommandQueue commandQueue(2 * 1024 * 1024);

		while (m_running)
		{
            m_window->PollEvents();

		    commandQueue.Submit<ClearCommand>(glm::vec4(0, 0, 1, 1));

		    m_rendererBackend->Execute(commandQueue, m_window->GetNativeHandle());
		    m_rendererBackend->Present(m_window->GetNativeHandle());
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