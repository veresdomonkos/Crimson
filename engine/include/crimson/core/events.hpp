#pragma once

#include <cstdint>
#include <functional>

#include <glm/vec2.hpp>

#include "crimson/core/keys.hpp"

namespace crimson
{
	enum class EventType
	{
		WindowClose,
        WindowResize,
        WindowFocus,
	    KeyPress,
        KeyRelease,
        TextInput,
        MouseButtonPress,
        MouseButtonRelease,
        MouseMove,
        MouseLeave,
        MouseScroll
	};

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual EventType GetType() const = 0;
		bool Handled;
	};

	using EventCallback = std::function<void(Event&)>;

	class EventDispatcher
	{
	public:
		EventDispatcher(Event& event) : m_event(event) {}

		template <typename T>
		bool Dispatch(const std::function<bool(T& event)>& handler)
		{
			if (m_event.GetType() == T::GetStaticType())
			{
				T& concreteEvent = static_cast<T&>(m_event);
				concreteEvent.Handled = handler(concreteEvent);
				return true;
			}

			return false;
		}

	private:
		Event& m_event;
	};

#define REGISTER_EVENT(x) \
static EventType GetStaticType() { return EventType::x; } \
virtual EventType GetType() const override { return GetStaticType(); }

	class WindowCloseEvent : public Event
	{
	public:
		REGISTER_EVENT(WindowClose)
	};

    class WindowResizeEvent : public Event
    {
    public:
        REGISTER_EVENT(WindowResize)

        explicit WindowResizeEvent(uint32_t width, uint32_t height)
            : m_width(width), m_height(height) {}

        [[nodiscard]] uint32_t GetWidth() const { return m_width; }
        [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    private:
        uint32_t m_width;
        uint32_t m_height;
    };

    class WindowFocusEvent : public Event
    {
    public:
        REGISTER_EVENT(WindowFocus)

        explicit WindowFocusEvent(bool focused) : m_focused(focused) {}

        [[nodiscard]] bool IsFocused() const { return m_focused; }
    private:
        bool m_focused;
    };

    class KeyPressEvent : public Event
    {
    public:
        REGISTER_EVENT(KeyPress)

        KeyPressEvent(Key key, KeyModifiers modifiers, bool isRepeat)
            : m_key(key), m_modifiers(modifiers), m_isRepeat(isRepeat) {}

        [[nodiscard]] Key GetKey() const { return m_key; }
        [[nodiscard]] KeyModifiers GetModifiers() const { return m_modifiers; }
        [[nodiscard]] bool IsRepeat() const { return m_isRepeat; }
    private:
        Key m_key;
        KeyModifiers m_modifiers;
        bool m_isRepeat;
    };

    class KeyReleaseEvent : public Event
    {
    public:
        REGISTER_EVENT(KeyRelease)

        KeyReleaseEvent(Key key, KeyModifiers modifiers) : m_key(key), m_modifiers(modifiers) {}

        [[nodiscard]] Key GetKey() const { return m_key; }
        [[nodiscard]] KeyModifiers GetModifiers() const { return m_modifiers; }
    private:
        Key m_key;
        KeyModifiers m_modifiers;
    };

    // A character typed, which the keyboard layout and the modifiers have decided, unlike the keys
    class TextInputEvent : public Event
    {
    public:
        REGISTER_EVENT(TextInput)

        explicit TextInputEvent(char32_t codepoint) : m_codepoint(codepoint) {}

        [[nodiscard]] char32_t GetCodepoint() const { return m_codepoint; }
    private:
        char32_t m_codepoint;
    };

    class MouseButtonPressEvent : public Event
    {
    public:
        REGISTER_EVENT(MouseButtonPress)

        MouseButtonPressEvent(MouseButton button, KeyModifiers modifiers) : m_button(button), m_modifiers(modifiers) {}

        [[nodiscard]] MouseButton GetButton() const { return m_button; }
        [[nodiscard]] KeyModifiers GetModifiers() const { return m_modifiers; }
    private:
        MouseButton m_button;
        KeyModifiers m_modifiers;
    };

    class MouseButtonReleaseEvent : public Event
    {
    public:
        REGISTER_EVENT(MouseButtonRelease)

        MouseButtonReleaseEvent(MouseButton button, KeyModifiers modifiers) : m_button(button), m_modifiers(modifiers) {}

        [[nodiscard]] MouseButton GetButton() const { return m_button; }
        [[nodiscard]] KeyModifiers GetModifiers() const { return m_modifiers; }
    private:
        MouseButton m_button;
        KeyModifiers m_modifiers;
    };

    class MouseMoveEvent : public Event
    {
    public:
        REGISTER_EVENT(MouseMove)

        // The position is in window coordinates, and the delta is how far the mouse moved since the event before
        MouseMoveEvent(glm::vec2 position, glm::vec2 delta) : m_position(position), m_delta(delta) {}

        [[nodiscard]] glm::vec2 GetPosition() const { return m_position; }
        [[nodiscard]] glm::vec2 GetDelta() const { return m_delta; }
    private:
        glm::vec2 m_position;
        glm::vec2 m_delta;
    };

    class MouseLeaveEvent : public Event
    {
    public:
        REGISTER_EVENT(MouseLeave)
    };

    class MouseScrollEvent : public Event
    {
    public:
        REGISTER_EVENT(MouseScroll)

        explicit MouseScrollEvent(glm::vec2 offset) : m_offset(offset) {}

        [[nodiscard]] glm::vec2 GetOffset() const { return m_offset; }
    private:
        glm::vec2 m_offset;
    };
}
