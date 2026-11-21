#include "crimson_editor/ui/stats_panel.hpp"

#include <format>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor::ui
{
    void StatsPanel::Draw()
    {
#ifdef NDEBUG
        constexpr const char* buildType = "Release";
#else
        constexpr const char* buildType = "Debug";
#endif
        ui::Text(std::format("{} | {}", m_api == GraphicsApi::Vulkan ? "Vulkan" : "OpenGL", buildType));

#ifndef NDEBUG
        ui::Text("Debug build, timings are not representative", ui::theme::Warning);
#endif

        ui::Separator();
        ui::Text(std::format("FPS: {:.0f}", m_stats.AverageFps));
        ui::Text(std::format("Frame: {:.3f} ms (worst {:.3f} ms)", m_stats.AverageFrameMs, m_stats.WorstFrameMs));
        ui::Text(std::format("CPU: {:.3f} ms", m_stats.AverageCpuMs));
        ui::Text(std::format("Waiting: {:.3f} ms", m_stats.AverageWaitMs));
        ui::Tooltip("Blocked on fences, image acquire, present or swap");

        ui::Separator();
        ui::Text(std::format("Draw calls: {}", m_stats.Render.DrawCalls));
        ui::Text(std::format("Triangles: {}", m_stats.Render.Triangles));
        ui::Text(std::format("Passes: {}", m_stats.Render.Passes));
        ui::Tooltip("Includes the editor UI pass. All other stats here exclude the UI");

        ui::Separator();
        ui::Text(std::format("Pipeline binds: {}", m_stats.Render.PipelineBinds));
        ui::Text(std::format("Material binds: {}", m_stats.Render.MaterialBinds));
        ui::Text(std::format("Buffer binds: {}", m_stats.Render.BufferBinds));
    }
}
