#include "glfw_window.hpp"
#include "crimson/core/log.hpp"
#include "crimson/renderer/renderer_api.hpp"

namespace crimson
{
	Unique<Window> Window::Create(WindowData data)
	{
		static bool s_glfwInitialized = false;

		if (!s_glfwInitialized)
		{
			if (!glfwInit())
			{
				LOG_ERROR("Failed to initialize GLFW!");
				return nullptr;
			}

			switch (RendererAPI::GetType())
			{
				case RendererAPIType::OpenGL:
					glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
					break;

				case RendererAPIType::Vulkan:
					glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
					break;

				default:
					break;
			}

			s_glfwInitialized = true;
		}

	    return CreateUnique<glfw::GLFWWindow>(std::move(data));
	}
}

namespace crimson::glfw
{
	GLFWWindow::GLFWWindow(WindowData data) : m_data(std::move(data)), m_handle(nullptr)
	{
		m_handle = glfwCreateWindow(static_cast<int>(m_data.Width), static_cast<int>(m_data.Height), m_data.Title.c_str(), nullptr, nullptr);

		if (!m_handle)
		{
			LOG_ERROR("Failed to create GLFW Window!");
			glfwTerminate();
			return;
		}

		glfwSetWindowUserPointer(m_handle, this);

		glfwSetWindowCloseCallback(m_handle, [](GLFWwindow* window) {
			auto* windowsWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			windowsWindow->m_data.EventCallbackFn(event);
		});
	}

    GLFWWindow::~GLFWWindow()
    {
	    if (m_handle)
	    {
	        glfwDestroyWindow(m_handle);
	    }

	    glfwTerminate();
    }

    void GLFWWindow::PollEvents()
	{
		glfwPollEvents();
	}
}