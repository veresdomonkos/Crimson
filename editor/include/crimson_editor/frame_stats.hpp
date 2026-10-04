#pragma once

namespace crimson::editor
{
    struct FrameStats
    {
        float FPS = 0.0f;
        float FrameTimeMs = 0.0f;
        float RenderMs = 0.0f;
        float FpsAccumulator = 0.0f;
        float UpdateTimer = 0.0f;
        int   FrameCount = 0;
    };
}