#pragma once

#include <array>

#include "crimson/core/window.hpp"
#include "../cursor_tracker.hpp"
#include "../window_graphics.hpp"

// Glad provides the OpenGL declarations
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace crimson::glfw
{
    class GLFWWindow : public Window, public WindowGraphics
	{
	public:
        GLFWWindow(GraphicsApi api, WindowData data);
        ~GLFWWindow() override;

		void PollEvents() override;

	    uint32_t Width() const override { return m_data.Width; }
	    uint32_t Height() const override { return m_data.Height; }

	    void SetTitle(const std::string& title) override;
	    void SetSize(uint32_t width, uint32_t height) override;
	    void Maximize() override { glfwMaximizeWindow(m_handle); }
	    void Restore() override { glfwRestoreWindow(m_handle); }
	    [[nodiscard]] bool IsFocused() const override { return glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) == GLFW_TRUE; }
	    [[nodiscard]] float GetContentScale() const override
	    {
	        float x = 1.0f, y = 1.0f;
	        glfwGetWindowContentScale(m_handle, &x, &y);
	        return x > 0.0f ? x : 1.0f;
	    }

        void SetCursorShape(CursorShape shape) override;

        [[nodiscard]] std::string GetClipboardText() const override;
        void SetClipboardText(const std::string& text) override;

        void SetCursorLocked(bool locked) override;
        [[nodiscard]] bool IsCursorLocked() const override { return m_cursor.IsLocked(); }

        [[nodiscard]] OSWindowHandle GetOSHandle() const override;
        [[nodiscard]] std::unique_ptr<GraphicsBackend> CreateGraphicsBackend() override;
	    void* GetNativeHandle() const override { return m_handle; }

        // WindowGraphics
        [[nodiscard]] std::vector<const char*> GetVulkanInstanceExtensions() const override;
        VkResult CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const override;
        void MakeContextCurrent() override { glfwMakeContextCurrent(m_handle); }
        void SetSwapInterval(int interval) override { glfwSwapInterval(interval); }
        void SwapBuffers() override { glfwSwapBuffers(m_handle); }
        [[nodiscard]] GLLoader GetGLLoader() const override;
	private:
        GraphicsApi m_api;
	    WindowData m_data;
		GLFWwindow* m_handle;
        CursorTracker m_cursor;

        static constexpr std::size_t kCursorShapeCount = static_cast<std::size_t>(CursorShape::NotAllowed) + 1;

        // A shape the platform has no cursor for stays null, which GLFW takes as the default arrow
        std::array<GLFWcursor*, kCursorShapeCount> m_shapes{};
        CursorShape m_shape = CursorShape::Arrow;
	};
}
