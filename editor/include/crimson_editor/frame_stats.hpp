#pragma once

#include <crimson/renderer/render_stats.hpp>

namespace crimson::editor
{
    struct FrameStats
    {
        // Published about once a second
        float AverageFps = 0.0f;
        float AverageFrameMs = 0.0f;
        float WorstFrameMs = 0.0f;
        float AverageCpuMs = 0.0f;
        float AverageWaitMs = 0.0f;

        // Last finished frame
        RenderStats Render;
        ShadowStats Shadows;

        double LastFrameTime = 0.0;
        double WindowSeconds = 0.0;
        double WindowWaitSeconds = 0.0;
        double WindowWorstSeconds = 0.0;
        int WindowFrames = 0;
    };
}
