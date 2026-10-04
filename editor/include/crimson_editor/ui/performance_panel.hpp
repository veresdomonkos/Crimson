#pragma once
#include "crimson_editor/ui/panel.hpp"
#include "crimson_editor/frame_stats.hpp"

namespace crimson::editor::ui
{
    class PerformancePanel : public Panel
    {
    public:
        explicit PerformancePanel(const FrameStats& stats) : m_stats(stats) {}
        void OnImGui() override;
    private:
        const FrameStats& m_stats;
    };
}