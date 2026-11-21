#pragma once
#include "crimson/graphics/graphics_device.hpp"
#include "../window_graphics.hpp"

namespace crimson::opengl
{
    class OpenGLDevice : public GraphicsDevice
    {
    public:
        explicit OpenGLDevice(WindowGraphics& windowGraphics);
        void WaitIdle() const override {}

        [[nodiscard]] WindowGraphics& GetWindowGraphics() const { return m_windowGraphics; }
    private:
        WindowGraphics& m_windowGraphics;
    };
}
