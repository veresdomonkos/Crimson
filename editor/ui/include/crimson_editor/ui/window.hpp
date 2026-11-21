#pragma once
#include <string_view>

#include <glm/vec2.hpp>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    enum class WindowKind
    {
        Normal,

        // Covers the whole of the editor's window, with nothing around it, and keeps no layout
        Fullscreen
    };

    // A window of its own, drawn while it lives
    class WindowScope
    {
    public:
        explicit WindowScope(std::string_view title, WindowKind kind = WindowKind::Normal);
        ~WindowScope();

        WindowScope(const WindowScope&) = delete;
        WindowScope& operator=(const WindowScope&) = delete;
    };

    // A small window over the others that keeps no layout, anchored at a point of the screen by its pivot, which is
    // (0, 0) for its top left and (1, 1) for its bottom right
    class OverlayWindow
    {
    public:
        OverlayWindow(std::string_view id, glm::vec2 anchor, glm::vec2 pivot, float alpha);
        ~OverlayWindow();

        OverlayWindow(const OverlayWindow&) = delete;
        OverlayWindow& operator=(const OverlayWindow&) = delete;

        explicit operator bool() const { return m_open; }

        [[nodiscard]] bool Hovered() const;
        [[nodiscard]] float Height() const;
    private:
        bool m_open;
    };

    // What the main menu leaves of the editor's window
    [[nodiscard]] Rect WorkArea();
}
