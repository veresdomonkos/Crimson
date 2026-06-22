#pragma once

#include <functional>
#include <string>
#include <memory>

#include "crimson/core/events.hpp"
#include "core.hpp"

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

	    virtual void* GetNativeHandle() const = 0;

	    static Unique<Window> Create(WindowData data);
	};
}
