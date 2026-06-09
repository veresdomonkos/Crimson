#include "windows_window.hpp"

#include <iostream>

namespace crimson
{
	std::unique_ptr<Window> Window::Create(const WindowData& windowData) { return std::make_unique<windows::WindowsWindow>(windowData); }
}

namespace crimson::windows
{
	static bool s_glfwInitialized = false;

	WindowsWindow::WindowsWindow(const WindowData& data) : Window(data), m_handle(nullptr)
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

		glfwMakeContextCurrent(m_handle);
		glfwSetWindowUserPointer(m_handle, this);

		glfwSetWindowCloseCallback(m_handle, [](GLFWwindow* window) {
			WindowsWindow* windowsWindow = static_cast<WindowsWindow*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			windowsWindow->m_eventCallbackFn(event);
		});
	}

	void WindowsWindow::PollEvents()
	{
		glfwPollEvents();
	}

	void WindowsWindow::Resize(int width, int height)
	{
		glfwSetWindowSize(m_handle, width, height);
	}

	void windows::WindowsWindow::SwapBuffers()
	{
		glfwSwapBuffers(m_handle);
	}
}