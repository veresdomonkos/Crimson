#include "glfw_window.hpp"

#include <iostream>

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
				std::cerr << "[Error] Failed to initialize GLFW!\n";
				return;
			}

			s_glfwInitialized = true;
		}

		m_handle = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);

		if (!m_handle)
		{
			std::cerr << "[Error] Failed to create GLFW Window!\n";
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