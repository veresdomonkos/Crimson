#include "crimson_editor/ui/stats_panel.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include <imgui.h>

namespace crimson::editor::ui
{
    void StatsPanel::OnImGui()
    {
        ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Stats");

#ifdef NDEBUG
        constexpr const char* buildType = "Release";
#else
        constexpr const char* buildType = "Debug";
#endif
        ImGui::Text("%s | %s", m_api == GraphicsApi::Vulkan ? "Vulkan" : "OpenGL", buildType);

#ifndef NDEBUG
        ImGui::TextColored(theme::Warning, "Debug build, timings are not representative");
#endif

        ImGui::Separator();
        ImGui::Text("FPS: %.0f", m_stats.AverageFps);
        ImGui::Text("Frame: %.3f ms (worst %.3f ms)", m_stats.AverageFrameMs, m_stats.WorstFrameMs);
        ImGui::Text("CPU: %.3f ms", m_stats.AverageCpuMs);
        ImGui::Text("Waiting: %.3f ms", m_stats.AverageWaitMs);
        ImGui::SetItemTooltip("Blocked on fences, image acquire, present or swap");

        ImGui::Separator();
        ImGui::Text("Draw calls: %u", m_stats.Render.DrawCalls);
        ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(m_stats.Render.Triangles));
        ImGui::Text("Passes: %u", m_stats.Render.Passes);
        ImGui::SetItemTooltip("Includes the editor UI pass. All other stats here exclude the UI");

        ImGui::Separator();
        ImGui::Text("Pipeline binds: %u", m_stats.Render.PipelineBinds);
        ImGui::Text("Material binds: %u", m_stats.Render.MaterialBinds);
        ImGui::Text("Buffer binds: %u", m_stats.Render.BufferBinds);

        ImGui::End();
    }
}
