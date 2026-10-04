#pragma once
#include "panel.hpp"

namespace crimson::editor::ui
{
    class InspectorPanel : public Panel
    {
    public:
        InspectorPanel() = default;
        void OnImGui() override
        {
            ImGui::Begin("Inspector");
            ImGui::End();
        }
    };
}
