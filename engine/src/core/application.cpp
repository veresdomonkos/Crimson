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
	    RenderPassInfo mainPassInfo {
	        .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
	        .ClearColor = glm::vec4(1, 0, 0, 1),
	    };

	    glm::vec3 vertices[] = { {-0.5f, -0.5, 0}, {0.5f, -0.5, 0}, {0, 0.5f, 0} };
	    VertexBufferInfo vInfo {
	        .Layout = { ShaderDataType::Float3 },
	        .Size = sizeof(vertices),
	        .Usage = BufferUsage::Static
	    };

	    VertexBufferHandle vertexBuffer = m_renderer->GetResourceManager().CreateVertexBuffer(vInfo, vertices);

	    uint32_t indices[] = {0, 1, 2};
	    IndexBufferInfo iInfo {
	        .Size = sizeof(indices),
	        .Usage = BufferUsage::Static,
	        .Type = IndexType::UInt32
	    };

	    IndexBufferHandle indexBuffer = m_renderer->GetResourceManager().CreateIndexBuffer(iInfo, indices);

		while (m_running)
		{
		    m_window->PollEvents();

            auto frame = m_renderer->BeginFrame(m_primarySurface);

		    if (!frame)
		        continue;

		    {
		        RenderPass mainPass = frame->BeginRenderPass(mainPassInfo);
		        mainPass.Draw(vertexBuffer, indexBuffer);
		    }

		    m_renderer->EndFrame(*frame);
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
