#pragma once

#include <functional>

namespace crimson
{
	enum class EventType
	{
		None,
		WindowClose
	};

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual EventType GetType() const = 0;

		bool IsHandled() { return m_handled; }
		void SetHandled(bool value) { m_handled = value; }
	private:
		bool m_handled;
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
				concreteEvent.SetHandled(handler(concreteEvent));
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
}