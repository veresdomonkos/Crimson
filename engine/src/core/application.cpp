#include "crimson/core/application.hpp"

#include "crimson/core/core.hpp"
#include "crimson/renderer/renderer_api.hpp"

namespace crimson
{
	Application::Application() : m_running(true)
	{
		RendererAPI::Init(RendererAPIType::OpenGL);
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
	    m_renderer = Renderer::Create();
	    m_primarySurface = m_renderer->Initialize(*m_window);
	}

    Application::~Application()
    {
        m_renderer->Shutdown();
    }

    void Application::Run()
	{
	    RenderPassInfo mainRenderPass {
	        .Target = m_renderer->GetResourceManager().GetBackBuffer(m_primarySurface),
	        .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
	        .ClearColor = glm::vec4(1, 0, 0, 1),
	    };

		while (m_running)
		{
		    m_window->PollEvents();

            Frame frame = m_renderer->BeginFrame(m_primarySurface);

		    frame.BeginRenderPass(mainRenderPass);
		    frame.EndRenderPass();

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
