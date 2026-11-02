#pragma once

#include <functional>
#include <string>
#include <memory>

#include "crimson/core/events.hpp"
#include "core.hpp"
#include "crimson/renderer/renderer_api.hpp"

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
		virtual ~Window() = default;

		virtual void PollEvents() = 0;
	    virtual uint32_t Width() const = 0;
	    virtual uint32_t Height() const = 0;

	    virtual void SetTitle(const std::string& title) = 0;
	    virtual void SetSize(uint32_t width, uint32_t height) = 0;

	    // A normal window filling the screen, with its title bar and the taskbar still there
	    virtual void Maximize() = 0;
	    virtual void Restore() = 0;

	    [[nodiscard]] virtual bool IsFocused() const = 0;

	    virtual void* GetNativeHandle() const = 0;

	    static Unique<Window> Create(RendererAPIType rendererAPI, WindowData data);
	};
}
