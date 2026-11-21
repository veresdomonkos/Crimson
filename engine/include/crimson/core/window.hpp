#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <memory>

#include "crimson/core/events.hpp"
#include "core.hpp"
#include "crimson/graphics/graphics_api.hpp"
#include "crimson/graphics/graphics_backend.hpp"

namespace crimson
{
	struct WindowData
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		EventCallback EventCallbackFn;
	};

    // What the mouse cursor looks like over the window. The diagonals are named by where they point to, up being the top
    // of the screen: ResizeDiagonalUp runs from bottom left to top right, ResizeDiagonalDown from top left to bottom right.
    enum class CursorShape
    {
        Arrow, TextInput, ResizeAll, ResizeVertical, ResizeHorizontal, ResizeDiagonalUp, ResizeDiagonalDown, Hand, NotAllowed
    };

    // The operating system's own window, for what has to name it outside the engine, such as the parent of a native
    // dialog. Value holds an HWND, an X11 Window, a wl_surface* or an NSWindow*.
    struct OSWindowHandle
    {
        enum class Kind { Win32, X11, Wayland, Cocoa };

        Kind Type;
        std::uintptr_t Value;
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

	    // How much bigger than at 100% things should be drawn, from the monitor's scaling, such as 1.5 at 150%
	    [[nodiscard]] virtual float GetContentScale() const = 0;

        // A locked cursor is hidden and stays where it was locked, while the mouse can keep moving forever without
        // stopping at the edge of the screen. The mouse move events keep reporting how far it moved.
        virtual void SetCursorLocked(bool locked) = 0;
        [[nodiscard]] virtual bool IsCursorLocked() const = 0;

        // Does nothing while the cursor is locked, as it is hidden then
        virtual void SetCursorShape(CursorShape shape) = 0;

        [[nodiscard]] virtual std::string GetClipboardText() const = 0;
        virtual void SetClipboardText(const std::string& text) = 0;

        [[nodiscard]] virtual OSWindowHandle GetOSHandle() const = 0;

        // Makes the graphics backend that draws to this window. It throws when the window's graphics API can't start,
        // and the window has to outlive the backend. Not const, as the OpenGL context is made current through the window.
        [[nodiscard]] virtual std::unique_ptr<GraphicsBackend> CreateGraphicsBackend() = 0;

	    static Unique<Window> Create(GraphicsApi rendererAPI, WindowData data);
	};
}
