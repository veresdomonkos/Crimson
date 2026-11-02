#pragma once
#include <functional>
#include <memory>
#include <vector>

#include <imgui.h>
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    class EditorUI
    {
    public:
        explicit EditorUI(std::function<void()> onCloseProject);
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
    private:
        void DrawMainMenu();
        void DrawDockSpace();
        void BuildDefaultLayout(ImGuiID dockSpace, const ImVec2& size);
    private:
        std::vector<Panel*> m_panels;
        std::function<void()> m_onCloseProject;
        bool m_resetLayout = false;
    };
}