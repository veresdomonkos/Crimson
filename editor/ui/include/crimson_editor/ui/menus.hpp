#pragma once
#include <functional>
#include <optional>
#include <string_view>

#include <glm/vec2.hpp>

namespace crimson::editor::ui
{
    // A submenu, whose items are drawn while it is open
    class Menu
    {
    public:
        explicit Menu(std::string_view label);
        ~Menu();

        Menu(const Menu&) = delete;
        Menu& operator=(const Menu&) = delete;

        explicit operator bool() const { return m_open; }
    private:
        bool m_open;
    };

    // An item of a menu. Returns true when it was picked.
    bool MenuItem(std::string_view label, std::string_view shortcut = {}, bool enabled = true, bool checked = false);

    // The menu of the item drawn last, opened by right-clicking it
    class ContextMenu
    {
    public:
        explicit ContextMenu(std::string_view id = {});
        ~ContextMenu();

        ContextMenu(const ContextMenu&) = delete;
        ContextMenu& operator=(const ContextMenu&) = delete;

        explicit operator bool() const { return m_open; }
    private:
        bool m_open;
    };

    // The menu of the window, opened by right-clicking where there is no item
    class BackgroundContextMenu
    {
    public:
        explicit BackgroundContextMenu(std::string_view id);
        ~BackgroundContextMenu();

        BackgroundContextMenu(const BackgroundContextMenu&) = delete;
        BackgroundContextMenu& operator=(const BackgroundContextMenu&) = delete;

        explicit operator bool() const { return m_open; }
    private:
        bool m_open;
    };

    struct PopupOptions
    {
        // On the screen, where it opens
        std::optional<glm::vec2> Position;

        // The arrow keys are the content's own and don't move through it
        bool NoNavigation = false;
    };

    // A window that opens over the others, from Open, and closes when it is clicked away from. It is drawn while it is open.
    class Popup
    {
    public:
        explicit Popup(std::string_view id, PopupOptions options = {});
        ~Popup();

        Popup(const Popup&) = delete;
        Popup& operator=(const Popup&) = delete;

        explicit operator bool() const { return m_open; }

        // The popup has just opened
        [[nodiscard]] bool IsAppearing() const;
        void Close();

        // By the id it is drawn with, from where it is drawn
        static void Open(std::string_view id);
        [[nodiscard]] static bool IsOpen(std::string_view id);
    private:
        bool m_open;
    };

    struct ModalOptions
    {
        // 0 sizes the window to its content. Otherwise it is centered, as wide as this much of the screen.
        float ScreenWidth = 0.0f;
    };

    // A window that blocks the rest until it is closed. It opens on the frame that shown turns true, and closes itself in the
    // frame that it turns false, whether that is before it or in its own content, so what the caller keeps is the only
    // record of whether it is open. The title can change while it is open, as the id is what the window is known by.
    class Modal
    {
    public:
        Modal(std::string_view title, std::string_view id, std::function<bool()> shown, ModalOptions options = {});
        ~Modal();

        Modal(const Modal&) = delete;
        Modal& operator=(const Modal&) = delete;

        explicit operator bool() const { return m_open; }

        [[nodiscard]] bool IsAppearing() const;
    private:
        std::function<bool()> m_shown;
        bool m_open;
    };
}
