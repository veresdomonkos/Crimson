#include "crimson/core/input.hpp"

namespace crimson
{
    // Nothing here marks an event handled, as whatever else wants it should still see it
    void Input::OnEvent(Event& event)
    {
        EventDispatcher dispatcher(event);

        dispatcher.Dispatch<KeyPressEvent>([this](KeyPressEvent& press)
        {
            m_keys.set(static_cast<std::size_t>(press.GetKey()));
            return false;
        });

        dispatcher.Dispatch<KeyReleaseEvent>([this](KeyReleaseEvent& release)
        {
            m_keys.reset(static_cast<std::size_t>(release.GetKey()));
            return false;
        });

        dispatcher.Dispatch<MouseButtonPressEvent>([this](MouseButtonPressEvent& press)
        {
            m_buttons.set(static_cast<std::size_t>(press.GetButton()));
            return false;
        });

        dispatcher.Dispatch<MouseButtonReleaseEvent>([this](MouseButtonReleaseEvent& release)
        {
            m_buttons.reset(static_cast<std::size_t>(release.GetButton()));
            return false;
        });

        dispatcher.Dispatch<MouseMoveEvent>([this](MouseMoveEvent& move)
        {
            m_mousePosition = move.GetPosition();
            m_pendingDelta += move.GetDelta();
            return false;
        });
    }

    void Input::Update()
    {
        m_mouseDelta = m_pendingDelta;
        m_pendingDelta = glm::vec2(0.0f);
    }

    bool Input::IsKeyDown(Key key) const
    {
        return m_keys.test(static_cast<std::size_t>(key));
    }

    bool Input::IsMouseButtonDown(MouseButton button) const
    {
        return m_buttons.test(static_cast<std::size_t>(button));
    }
}
