#pragma once

#include <crimson/graphics/graphics_api.hpp>
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor
{
    class StatsPanel : public ui::Panel
    {
    public:
        StatsPanel(const FrameStats& stats, GraphicsApi api)
            : ui::Panel({ .Title = "Stats", .Icon = ui::icons::kInfo, .Slot = ui::DockSlot::RightBottom, .TabOrder = 1, .DefaultSize = { 15.0f, 0.0f } }),
              m_stats(stats), m_api(api) {}
    protected:
        void Draw() override;
    private:
        const FrameStats& m_stats;
        GraphicsApi m_api;
    };
}
