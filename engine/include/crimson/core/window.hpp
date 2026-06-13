#pragma once

#include <functional>
#include <string>
#include <memory>
#include <crimson/core/events.hpp>
#include <crimson/renderer/rendering_context.hpp>

namespace crimson
{
	struct WindowData
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		EventCallback EventCallbackFn;
	};

	class Window
	{
	public:
		Window(const WindowData& data) : m_title(data.Title), m_width(data.Width), m_height(data.Height), m_eventCallbackFn(data.EventCallbackFn)
		{

		};

		virtual ~Window() = default;

	    void MakeCurrent() { m_renderingContext->MakeCurrent(); }
	    void SwapBuffers() { m_renderingContext->SwapBuffers(); }

		virtual void PollEvents() = 0;
		virtual void Resize(int width, int height) = 0;

	    virtual void* GetNativeHandle() = 0;

		static std::unique_ptr<Window> Create(const WindowData& windowData);
	protected:
		std::string m_title;
		uint32_t m_width;
		uint32_t m_height;
		EventCallback m_eventCallbackFn;
	    std::unique_ptr<RenderingContext> m_renderingContext;
	};
}