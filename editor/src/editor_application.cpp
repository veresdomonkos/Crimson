#include "editor/editor_application.hpp"

#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/renderer_api.hpp>

#include "editor/utils.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <glfw/glfw3.h>

namespace crimson::editor
{
	EditorApplication::EditorApplication() : m_running(true)
	{
		RendererAPI::Init(RendererAPIType::OpenGL);
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
	    m_renderer = Renderer::Create();
	    m_primarySurface = m_renderer->Initialize(*m_window);

	    m_cameraPosition = glm::vec3(0.0f, 0.15f, 3.0f);

	    m_camera = {
	        .View = glm::lookAt(m_cameraPosition, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            .Proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 1000.0f)
        };
	}

    EditorApplication::~EditorApplication()
    {
        m_renderer->Shutdown();
    }

    void EditorApplication::Run()
	{
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

	    const char* vertexShaderSrc = R"(
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

	    const char* fragmentShaderSrc = R"(
            #version 450 core

            layout(location = 0) in vec4 v_Color;
            layout(location = 0) out vec4 outColor;

            layout(std140, set = 1, binding = 1) uniform MaterialBlock
            {
                vec4 u_Color;
            };

            void main()
            {
                outColor = u_Color;
            }
        )";

	    const char * vertexShaderSrc2 = R"(
            #version 450 core
            layout(std140, set = 0, binding = 0) uniform CameraBlock
            {
                mat4 u_View;
                mat4 u_Projection;
            };

            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Color;

            layout(location = 0) out vec4 v_Color;

            void main()
            {
                gl_Position = u_Projection * u_View * vec4(a_Position, 1.0);
                v_Color = vec4(1.0, 1.0, 1.0, 1.0);
            }
        )";

	    ShaderHandle shader = m_renderer->GetResourceManager().CreateShader(
	        utils::CompileGLSLToSPIRV(vertexShaderSrc2, "vertex"),
	        utils::CompileGLSLToSPIRV(fragmentShaderSrc, "fragment")
	    );

	    MaterialHandle mat = m_renderer->GetResourceManager().CreateMaterial(shader);
	    m_renderer->GetResourceManager().SetMaterialPropertyByName(mat, "u_Color", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

	    RenderPassInfo mainPassInfo {
	        .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(1, 0, 0, 1),
            .Camera = m_camera
        };

		while (m_running)
		{
		    mainPassInfo.Camera = m_camera;

		    m_window->PollEvents();

            auto frame = m_renderer->BeginFrame(m_primarySurface);

		    if (frame.ShouldRender())
		    {
		        auto& mainPass = frame.BeginRenderPass(mainPassInfo);
		        mainPass.Draw({vertexBuffer, indexBuffer, mat});
		    }

		    m_renderer->EndFrame(frame);
		}
	}

	void EditorApplication::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			m_running = false;
			return true;
		});

	    dispatcher.Dispatch<KeyPressEvent>([this](KeyPressEvent& event) {
	        glm::vec3 move(0.0f);

	        if (event.GetKeyCode() == GLFW_KEY_A)
	            move.x -= 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_D)
                move.x += 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_W)
                move.z -= 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_S)
                move.z += 0.1f;

	        m_cameraPosition += move;
            m_camera.View = glm::lookAt(m_cameraPosition, m_cameraPosition + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	        return true;
        });
	}
}
