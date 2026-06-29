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

	    struct Vertex
	    {
	        glm::vec3 Position;
	        glm::vec3 Color;
	    };

	    Vertex vertices[] = {
	        {{-0.5f, -0.5f, 0.0f}, {0.65f, 0.32f, 0.43f}},
            {{ 0.5f, -0.5f, 0.0f}, {0.21f, 0.34f, 0.87f}},
            {{ 0.0f,  0.5f, 0.0f}, {0.36f, 0.65f, 0.43f}}
	    };

	    VertexBufferInfo vInfo {
	        .Layout = { ShaderDataType::Float3, ShaderDataType::Float3 },
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

	    const char* vertexShader = R"(
        #version 450

        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inColor;

        layout(location = 0) out vec4 fragColor;

        void main()
        {
            gl_Position = vec4(inPosition, 1.0);
            fragColor = vec4(inColor, 1.0);
        }
        )";

	    const char* fragmentShader = R"(
        #version 450

        layout(location = 0) in vec4 fragColor;

        layout(location = 0) out vec4 outColor;

        void main()
        {
            outColor = fragColor;
        }
        )";

	    ShaderHandle shader = m_renderer->GetResourceManager().CreateShader(vertexShader, fragmentShader);

		while (m_running)
		{
		    m_window->PollEvents();

            auto frame = m_renderer->BeginFrame(m_primarySurface);

		    if (!frame.ShouldRender())
		        continue;

		    RenderPass& mainPass = frame.BeginRenderPass(mainPassInfo);
            mainPass.Draw({.VertexBuffer = vertexBuffer, .IndexBuffer = indexBuffer, .Shader = shader});

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
