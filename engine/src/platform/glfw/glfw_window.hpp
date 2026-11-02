#pragma once

#include "crimson/core/window.hpp"
#include <GLFW/glfw3.h>

namespace crimson::glfw
{
	class GLFWWindow : public Window
	{
	public:
		explicit GLFWWindow(WindowData data);
        ~GLFWWindow() override;

		void PollEvents() override;

	    uint32_t Width() const override { return m_data.Width; }
	    uint32_t Height() const override { return m_data.Height; }

	    void SetTitle(const std::string& title) override;
	    void SetSize(uint32_t width, uint32_t height) override;
	    void Maximize() override { glfwMaximizeWindow(m_handle); }
	    void Restore() override { glfwRestoreWindow(m_handle); }
	    [[nodiscard]] bool IsFocused() const override { return glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) == GLFW_TRUE; }

	    void* GetNativeHandle() const override { return m_handle; }
	private:
	    WindowData m_data;
		GLFWwindow* m_handle;
	};
}