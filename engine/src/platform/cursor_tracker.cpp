#include "cursor_tracker.hpp"

namespace crimson
{
    MouseMoveEvent CursorTracker::Move(glm::vec2 position)
    {
        const glm::vec2 delta = m_skipNextDelta ? glm::vec2(0.0f) : position - m_position;

        m_position = position;
        m_skipNextDelta = false;

        return MouseMoveEvent(m_locked ? m_lockedAt : position, delta);
    }

    void CursorTracker::Lock(glm::vec2 position)
    {
        m_locked = true;
        m_lockedAt = position;
        m_skipNextDelta = true;
    }

    glm::vec2 CursorTracker::Unlock()
    {
        m_locked = false;
        m_skipNextDelta = true;

        return m_lockedAt;
    }
}
