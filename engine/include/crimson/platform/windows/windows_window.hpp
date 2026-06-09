#pragma once

#include "crimson/core/window.hpp";
#include <GLFW/glfw3.h>

namespace crimson::windows
{
	class WindowsWindow : public Window
	{
	public:

		WindowsWindow(const WindowData& data);

		virtual void PollEvents() override;
		virtual void Resize(int width, int height) override;
		virtual void SwapBuffers() override;
	private:
		GLFWwindow* m_handle;
	};
}