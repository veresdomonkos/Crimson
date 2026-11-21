#pragma once
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "crimson_editor/ui/commands.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // The editor's window: the main menu, the dock space and the panels in it
    class Workspace
    {
    public:
        Workspace();
        ~Workspace();

        Workspace(const Workspace&) = delete;
        Workspace& operator=(const Workspace&) = delete;

        template <typename T, typename... Args>
        T& AddPanel(Args&&... args)
        {
            auto panel = std::make_unique<T>(std::forward<Args>(args)...);
            T& added = *panel;
            Add(std::move(panel));
            return added;
        }

        // A command that works whichever panel is focused
        void AddCommand(Command command);

        // The main menu and its shortcuts, the dock space, and then every open panel
        void Draw();

        [[nodiscard]] std::span<const std::unique_ptr<Panel>> Panels() const { return m_panels; }
    private:
        void Add(std::unique_ptr<Panel> panel);
        void DrawMainMenu();
        void DrawViewMenu();
        void DrawDockSpace();
        void DrawPanel(Panel& panel);
        void DockWithSiblings(const Panel& panel);
    private:
        std::vector<std::unique_ptr<Panel>> m_panels;

        // The same panels in the order they are drawn in, which also sets their tabs
        std::vector<Panel*> m_drawOrder;

        std::vector<Command> m_commands;
        bool m_resetLayout = false;
    };
}
