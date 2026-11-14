#pragma once
#include <functional>
#include <memory>
#include <vector>

#include <imgui.h>
#include "crimson_editor/ui/notifications.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // What the main menu's own items do, the panels add theirs
    struct MainMenuActions
    {
        std::function<void()> SaveAll;
        std::function<void()> CloseProject;
        std::function<void()> Undo;
        std::function<void()> Redo;
        std::function<bool()> CanUndo;
        std::function<bool()> CanRedo;
    };

    class EditorUI
    {
    public:
        explicit EditorUI(MainMenuActions actions);
        ~EditorUI();

        template<typename T, typename... Args>
        T& AddPanel(Args&&... args)
        {
            auto panel = new T(std::forward<Args>(args)...);
            T& ref = *panel;
            m_panels.push_back(panel);
            return ref;
        }

        void Draw();

        // The messages in the corner of the window, for what the editor wants noticed
        [[nodiscard]] Notifications& GetNotifications() { return m_notifications; }
    private:
        void DrawMainMenu();
        void DrawDockSpace();
        void BuildDefaultLayout(ImGuiID dockSpace, const ImVec2& size);
    private:
        std::vector<Panel*> m_panels;
        MainMenuActions m_actions;
        bool m_resetLayout = false;
        Notifications m_notifications;
    };
}
