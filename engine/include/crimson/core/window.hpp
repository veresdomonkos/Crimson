#pragma once

#include <functional>
#include <string>
#include <memory>
#include <crimson/core/events.hpp>

namespace crimson
{
	struct WindowData
	{
		std::string Title;
		int Width;
		int Height;
		EventCallback EventCallbackFn;
	};

	class Window
	{
	public:
		Window(const WindowData& data) : m_title(data.Title), m_width(data.Width), m_height(data.Height), m_eventCallbackFn(data.EventCallbackFn)
		{

		};

		virtual ~Window() {}
			
		virtual void PollEvents() = 0;
		virtual void Resize(int width, int height) = 0;
		virtual void SwapBuffers() = 0;

		static std::unique_ptr<Window> Create(const WindowData& windowData);
	protected:
		std::string m_title;
		int m_width;
		int m_height;
		EventCallback m_eventCallbackFn;
	};
}