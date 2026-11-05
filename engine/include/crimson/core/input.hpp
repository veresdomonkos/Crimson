#pragma once
#include <glm/vec2.hpp>

#include "crimson/core/window.hpp"

namespace crimson
{
    enum class Key
    {
        A, D, E, F, Q, S, W,
        Up, Down, Left, Right,
        LeftShift, RightShift, LeftAlt, RightAlt
    };

    enum class MouseButton
    {
        Left, Right, Middle
    };

    // The keyboard and mouse state of a window
    class Input
    {
    public:
        explicit Input(const Window& window);

        // Call once a frame, after the window polled its events
        void Update();

        [[nodiscard]] bool IsKeyDown(Key key) const;
        [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const;

        // How far the mouse moved since the last Update, in pixels
        [[nodiscard]] glm::vec2 GetMouseDelta() const { return m_mouseDelta; }

        // A locked cursor is hidden and stays where it was locked, while the mouse can keep moving forever without
        // stopping at the edge of the screen
        void SetCursorLocked(bool locked);
        [[nodiscard]] bool IsCursorLocked() const { return m_locked; }
    private:
        void* m_window;
        glm::dvec2 m_mousePosition{0.0};
        glm::dvec2 m_lockedAt{0.0};
        glm::vec2 m_mouseDelta{0.0f};
        bool m_skipNextDelta = true;
        bool m_locked = false;
    };
}
