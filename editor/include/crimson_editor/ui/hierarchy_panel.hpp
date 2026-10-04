#pragma once
#include "panel.hpp"

namespace crimson::editor::ui
{
    class HierarchyPanel : public Panel
    {
    public:
        HierarchyPanel() = default;
        void OnImGui() override
        {
            ImGui::Begin("Hierarchy");
            ImGui::End();
        }
    };
}
