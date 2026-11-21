#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include <crimson/core/window.hpp>

namespace crimson::tests
{
    // A Window without a screen or a GPU, for what only needs to talk to one: it keeps the clipboard text, the cursor
    // shape and the size it is given, and it can't make a graphics backend
    class FakeWindow : public Window
    {
    public:
        void PollEvents() override {}
        uint32_t Width() const override { return m_width; }
        uint32_t Height() const override { return m_height; }

        void SetTitle(const std::string& title) override { Title = title; }
        void SetSize(uint32_t width, uint32_t height) override { m_width = width; m_height = height; }

        void Maximize() override {}
        void Restore() override {}

        [[nodiscard]] bool IsFocused() const override { return true; }
        [[nodiscard]] float GetContentScale() const override { return 1.0f; }

        void SetCursorLocked(bool locked) override { m_cursorLocked = locked; }
        [[nodiscard]] bool IsCursorLocked() const override { return m_cursorLocked; }

        void SetCursorShape(CursorShape shape) override { Shape = shape; }

        [[nodiscard]] std::string GetClipboardText() const override { return Clipboard; }
        void SetClipboardText(const std::string& text) override { Clipboard = text; }

        [[nodiscard]] OSWindowHandle GetOSHandle() const override { return { OSWindowHandle::Kind::Win32, 0 }; }

        [[nodiscard]] std::unique_ptr<GraphicsBackend> CreateGraphicsBackend() override { return nullptr; }

        std::string Title;
        std::string Clipboard;
        CursorShape Shape = CursorShape::Arrow;
    private:
        uint32_t m_width = 800;
        uint32_t m_height = 600;
        bool m_cursorLocked = false;
    };
}
