#pragma once

#include <crimson/renderer/renderer_api.hpp>
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    class StatsPanel : public Panel
    {
    public:
        StatsPanel(const FrameStats& stats, RendererAPIType api) : m_stats(stats), m_api(api) {}
        void OnImGui() override;
    private:
        const FrameStats& m_stats;
        RendererAPIType m_api;
    };
}
