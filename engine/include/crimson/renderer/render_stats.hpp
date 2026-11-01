#pragma once

#include <chrono>
#include <cstdint>

namespace crimson
{
    struct RenderStats
    {
        uint32_t DrawCalls = 0;
        uint64_t Triangles = 0;
        uint32_t Passes = 0;
        double WaitSeconds = 0.0; // blocked on fences, image acquire, present or swap
    };

    class ScopedWaitTimer
    {
    public:
        explicit ScopedWaitTimer(RenderStats& stats)
            : m_stats(stats), m_start(std::chrono::steady_clock::now())
        {
        }

        ~ScopedWaitTimer()
        {
            m_stats.WaitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
        }

        ScopedWaitTimer(const ScopedWaitTimer&) = delete;
        ScopedWaitTimer& operator=(const ScopedWaitTimer&) = delete;

    private:
        RenderStats& m_stats;
        std::chrono::steady_clock::time_point m_start;
    };
}
