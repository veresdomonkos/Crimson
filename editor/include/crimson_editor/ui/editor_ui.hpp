#pragma once
#include <memory>
#include <vector>

#include <imgui.h>
#include <crimson/ui/imgui_backend.hpp>
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    class EditorUI
    {
    public:
        EditorUI(ImGuiBackend& backend);
        ~EditorUI();

        template<typename T, typename... Args>
        T& AddPanel(Args&&... args)
        {
            auto panel = new T(std::forward<Args>(args)...);
            T& ref = *panel;
            m_panels.push_back(panel);
            return ref;
        }

        void Draw(const NativeFrameHandles& handles);
    private:
        void ApplyEditorStyle();
        void DrawMainMenu();
        void DrawDockSpace();
        void BuildDefaultLayout(ImGuiID dockSpace, const ImVec2& size);
    private:
        std::vector<Panel*> m_panels;
        ImGuiBackend& m_backend;
        bool m_resetLayout = false;
    };
}