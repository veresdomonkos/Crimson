#pragma once
#include "imgui.h"
#include "panel.hpp"

namespace crimson::editor::ui
{
    class AssetBrowserPanel : public Panel
    {
    public:
        AssetBrowserPanel() = default;
        void OnImGui() override
        {
            ImGui::Begin("Assets");
            ImGui::End();
        }
    };
}
