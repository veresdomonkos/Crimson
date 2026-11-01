#include "crimson/core/input.hpp"

#include <GLFW/glfw3.h>

namespace crimson
{
    static int ToGlfwKey(Key key)
    {
        switch (key)
        {
            case Key::A:          return GLFW_KEY_A;
            case Key::D:          return GLFW_KEY_D;
            case Key::E:          return GLFW_KEY_E;
            case Key::F:          return GLFW_KEY_F;
            case Key::Q:          return GLFW_KEY_Q;
            case Key::S:          return GLFW_KEY_S;
            case Key::W:          return GLFW_KEY_W;
            case Key::Up:         return GLFW_KEY_UP;
            case Key::Down:       return GLFW_KEY_DOWN;
            case Key::Left:       return GLFW_KEY_LEFT;
            case Key::Right:      return GLFW_KEY_RIGHT;
            case Key::LeftShift:  return GLFW_KEY_LEFT_SHIFT;
            case Key::RightShift: return GLFW_KEY_RIGHT_SHIFT;
            case Key::LeftAlt:    return GLFW_KEY_LEFT_ALT;
            case Key::RightAlt:   return GLFW_KEY_RIGHT_ALT;
        }

        return GLFW_KEY_UNKNOWN;
    }

    static int ToGlfwButton(MouseButton button)
    {
        switch (button)
        {
            case MouseButton::Left:   return GLFW_MOUSE_BUTTON_LEFT;
            case MouseButton::Right:  return GLFW_MOUSE_BUTTON_RIGHT;
            case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        }

        return GLFW_MOUSE_BUTTON_LEFT;
    }

    Input::Input(const Window& window)
        : m_window(window.GetNativeHandle())
    {
    }

    void Input::Update()
    {
        glm::dvec2 position;
        glfwGetCursorPos(static_cast<GLFWwindow*>(m_window), &position.x, &position.y);

        m_mouseDelta = m_skipNextDelta ? glm::vec2(0.0f) : glm::vec2(position - m_mousePosition);
        m_mousePosition = position;
        m_skipNextDelta = false;
    }

    bool Input::IsKeyDown(Key key) const
    {
        return glfwGetKey(static_cast<GLFWwindow*>(m_window), ToGlfwKey(key)) == GLFW_PRESS;
    }

    bool Input::IsMouseButtonDown(MouseButton button) const
    {
        return glfwGetMouseButton(static_cast<GLFWwindow*>(m_window), ToGlfwButton(button)) == GLFW_PRESS;
    }

    void Input::SetCursorLocked(bool locked)
    {
        auto* window = static_cast<GLFWwindow*>(m_window);

        if (locked)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        else
        {
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // The cursor position jumps when the mode changes, which isn't the mouse moving
        m_skipNextDelta = true;
    }
}
