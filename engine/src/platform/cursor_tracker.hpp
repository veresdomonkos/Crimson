#pragma once

#include <glm/vec2.hpp>

#include "crimson/core/events.hpp"

namespace crimson
{
    // Turns the cursor positions a window reports into mouse move events, and keeps the cursor of a locked window
    // where it was locked. It uses no windowing library, so every platform's window can use it.
    class CursorTracker
    {
    public:
        // The event for the cursor at a position. While locked, the position in it is where the cursor was locked, so
        // the UI never sees the hidden cursor wander, and the delta is still how far the mouse really moved.
        [[nodiscard]] MouseMoveEvent Move(glm::vec2 position);

        // The cursor is at the position as it gets locked
        void Lock(glm::vec2 position);

        // Returns where to put the cursor back
        [[nodiscard]] glm::vec2 Unlock();

        [[nodiscard]] bool IsLocked() const { return m_locked; }
    private:
        glm::vec2 m_position{0.0f};
        glm::vec2 m_lockedAt{0.0f};
        bool m_locked = false;

        // The position jumps when the cursor mode changes, which is not the mouse moving, so the first position
        // after a change, and the first one ever, only becomes the origin
        bool m_skipNextDelta = true;
    };
}
