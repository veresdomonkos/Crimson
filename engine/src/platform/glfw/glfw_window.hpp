#pragma once

#include "crimson/core/window.hpp"
#include <GLFW/glfw3.h>

namespace crimson::glfw
{
	class GLFWWindow : public Window
	{
	public:
		GLFWWindow(const WindowData& data);
        ~GLFWWindow() override;
		void PollEvents() override;
		void Resize(int width, int height) override;

	    void* GetNativeHandle() override { return m_handle; }
	private:
		GLFWwindow* m_handle;
	};
}