#pragma once
#include "crimson/core/window.hpp"
#include "crimson/graphics/graphics_device.hpp"

#define GLFW_INCLUDE_NONE
#include "glfw/glfw3.h"

namespace crimson::opengl
{
    class OpenGLDevice : public GraphicsDevice
    {
    public:
        explicit OpenGLDevice(const Window &window);
        void WaitIdle() const override {}

        [[nodiscard]] GLFWwindow* GetPrimaryWindow() const { return m_primaryWindow; }
    private:
        GLFWwindow* m_primaryWindow;
    };
}
