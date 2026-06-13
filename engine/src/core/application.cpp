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

	    float vertices[] = {
	        -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.0f,  0.5f, 0.0f
        };

	    uint32_t indices[] = {
	        0, 1, 2
        };

	    auto vertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));
	    auto indexBuffer = IndexBuffer::Create(indices, 3);

	    vertexBuffer->SetLayout({
	        { ShaderDataType::Float3, "a_Position" }
        });

		while (m_running)
		{
            m_window->PollEvents();

		    Renderer::BeginScene();
		    Renderer::Clear(glm::vec4(0.8f, 0.1f, 0.2f, 1.0f));
		    Renderer::DrawIndexed(vertexBuffer, indexBuffer);
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
