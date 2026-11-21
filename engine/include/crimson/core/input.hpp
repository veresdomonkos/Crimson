#pragma once

#include <bitset>

#include <glm/vec2.hpp>

#include "crimson/core/events.hpp"
#include "crimson/core/keys.hpp"

namespace crimson
{
    // The keyboard and mouse state of a window, kept up to date from the events the window sends
    class Input
    {
    public:
        // Give it every event the window sends
        void OnEvent(Event& event);

        // Call once a frame, after the window polled its events
        void Update();

        [[nodiscard]] bool IsKeyDown(Key key) const;
        [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const;

        // In window coordinates, where the last mouse move event put it
        [[nodiscard]] glm::vec2 GetMousePosition() const { return m_mousePosition; }

        // How far the mouse moved since the last Update, in pixels
        [[nodiscard]] glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
    private:
        std::bitset<kKeyCount> m_keys;
        std::bitset<kMouseButtonCount> m_buttons;

        glm::vec2 m_mousePosition{0.0f};
        glm::vec2 m_mouseDelta{0.0f};

        // What the move events added up to since the last Update
        glm::vec2 m_pendingDelta{0.0f};
    };
}
