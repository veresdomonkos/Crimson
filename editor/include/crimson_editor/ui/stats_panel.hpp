#pragma once

#include <crimson/graphics/graphics_api.hpp>
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    class StatsPanel : public Panel
    {
    public:
        StatsPanel(const FrameStats& stats, GraphicsApi api)
            : Panel({ .Title = "Stats", .Icon = icons::kInfo, .Slot = DockSlot::RightBottom, .TabOrder = 1, .DefaultSize = { 15.0f, 0.0f } }),
              m_stats(stats), m_api(api) {}
    protected:
        void Draw() override;
    private:
        const FrameStats& m_stats;
        GraphicsApi m_api;
    };
}
