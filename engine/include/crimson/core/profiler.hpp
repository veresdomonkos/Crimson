#pragma once
#include <cstdint>

#include <tracy/Tracy.hpp>

#define CRIMSON_PROFILE_FUNCTION() ZoneScoped
#define CRIMSON_PROFILE_SCOPE(name) ZoneScopedN(name)
// For a name only known at runtime, like a pass's. The name has to be null terminated.
#define CRIMSON_PROFILE_SCOPE_DYNAMIC(name) ZoneTransientN(___crimson_dynamic_zone, name, true)
#define CRIMSON_PROFILE_FRAME() FrameMark
#define CRIMSON_PROFILE_PLOT(name, value) TracyPlot(name, static_cast<int64_t>(value))

namespace crimson
{
    // Runs the profiler for as long as it lives. A program makes one at the start of main, before anything that is
    // profiled, and keeps it until all of that is gone. Profiled code must not run outside of one.
    class ProfilerSession
    {
    public:
#ifdef TRACY_ENABLE
        ProfilerSession() { tracy::StartupProfiler(); }
        ~ProfilerSession() { tracy::ShutdownProfiler(); }
#else
        ProfilerSession() = default;
#endif

        ProfilerSession(const ProfilerSession&) = delete;
        ProfilerSession& operator=(const ProfilerSession&) = delete;
    };
}
