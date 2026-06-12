#include "glfw_window.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
	std::unique_ptr<Window> Window::Create(const WindowData& windowData) { return std::make_unique<glfw::GLFWWindow>(windowData); }
}

namespace crimson::glfw
{
	static bool s_glfwInitialized = false;

	GLFWWindow::GLFWWindow(const WindowData& data) : Window(data), m_handle(nullptr)
	{
		if (!s_glfwInitialized)
		{
			if (!glfwInit())
			{
				LOG_ERROR("[Error] Failed to initialize GLFW!");
				return;
			}

			s_glfwInitialized = true;
		}

		m_handle = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);

		if (!m_handle)
		{
			LOG_ERROR("[Error] Failed to create GLFW Window!");
			glfwTerminate();
			return;
		}

		glfwSetWindowUserPointer(m_handle, this);

		glfwSetWindowCloseCallback(m_handle, [](GLFWwindow* window) {
			auto* windowsWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			windowsWindow->m_eventCallbackFn(event);
		});
	}

	void GLFWWindow::PollEvents()
	{
		glfwPollEvents();
	}

	void GLFWWindow::Resize(int width, int height)
	{
		glfwSetWindowSize(m_handle, width, height);
	}
}