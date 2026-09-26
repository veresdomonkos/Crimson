#pragma once

#include <cstdint>
#include <functional>

namespace crimson
{
	enum class EventType
	{
		None,
		WindowClose,
	    KeyPress,
	    WindowResize
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

    using KeyCode = std::uint16_t;

    class KeyPressEvent : public Event
    {
    public:
        REGISTER_EVENT(KeyPress)

        explicit KeyPressEvent(KeyCode keyCode, bool isRepeat = false)
            : m_keyCode(keyCode), m_isRepeat(isRepeat) {}

        [[nodiscard]] KeyCode GetKeyCode() const { return m_keyCode; }
        [[nodiscard]] bool IsRepeat() const { return m_isRepeat; }
    private:
        KeyCode m_keyCode;
        bool m_isRepeat = false;
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
}