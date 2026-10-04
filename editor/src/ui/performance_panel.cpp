#include "editor/ui/performance_panel.hpp"
#include <imgui.h>

namespace crimson::editor::ui
{
    void PerformancePanel::OnImGui()
    {
        ImGui::Begin("Performance");
        ImGui::Text("FPS: %.1f", m_stats.FPS);
        ImGui::Text("Frame Time: %.3f ms", m_stats.FrameTimeMs);
        ImGui::Separator();
        ImGui::Text("Render: %.3f ms", m_stats.RenderMs);
        ImGui::End();
    }
}