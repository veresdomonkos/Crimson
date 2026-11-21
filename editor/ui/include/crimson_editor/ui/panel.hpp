#pragma once
#include <span>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "crimson_editor/ui/commands.hpp"

namespace crimson::editor::ui
{
    // The areas of the default layout, which a panel is docked into when there is no saved layout
    enum class DockSlot
    {
        Left,
        Center,
        Bottom,
        Right,
        RightBottom,
        Floating
    };

    // What a panel says about itself, for the workspace to open its window and list it in View
    struct PanelInfo
    {
        // The window's title, and its key in the saved layout. Changing one loses where users put that window.
        std::string Title;

        // Before its name in View
        const char* Icon = nullptr;

        DockSlot Slot = DockSlot::Center;

        // Among the panels of the same slot, lowest first and in front
        int TabOrder = 0;

        // Has a close button, and a toggle in View
        bool Closable = true;

        // Closed panels are opened from View, or by Show
        bool OpenAtStart = true;

        // For an image: no padding, scrollbars or keyboard navigation
        bool FillsWindow = false;

        // In font sizes, for a window with no saved size. 0 is left to ImGui.
        glm::vec2 DefaultSize{};
    };

    class Workspace;

    // A window of the editor. The workspace opens the window, draws the panel in it, and lays out the windows.
    class Panel
    {
    public:
        explicit Panel(PanelInfo info);
        virtual ~Panel() = default;

        Panel(const Panel&) = delete;
        Panel& operator=(const Panel&) = delete;

        [[nodiscard]] const PanelInfo& Info() const { return m_info; }

        // As of the last frame the panel was drawn in. A closed panel is neither.
        [[nodiscard]] bool IsOpen() const { return m_open; }
        [[nodiscard]] bool IsFocused() const { return m_focused; }
        [[nodiscard]] bool IsHovered() const { return m_hovered; }

        // Opens the panel if it is closed, and brings it to the front and focuses it on the next frame
        void Show();

        // Closes the panel, which isn't drawn until Show opens it again
        void Close() { m_open = false; }

        [[nodiscard]] std::span<const Command> Commands() const { return m_commands; }
    protected:
        // Adds a command that is in the menu while the panel is open, and whose shortcut works while it is focused
        void AddCommand(Command command);

        // Draws what is inside the window, which the workspace has opened. Called whether or not the window is visible, as a
        // hidden tab still has its work to do.
        virtual void Draw() = 0;

        // Called when Show opens the panel
        virtual void OnOpened() {}
    private:
        friend class Workspace;

        PanelInfo m_info;
        std::vector<Command> m_commands;

        bool m_open;
        bool m_focused = false;
        bool m_hovered = false;
        bool m_focusRequested = false;

        // The workspace is drawing the panel, so its window is the current one
        bool m_drawing = false;
    };
}
