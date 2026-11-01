#pragma once

#include <tracy/Tracy.hpp>

#define CRIMSON_PROFILE_FUNCTION() ZoneScoped
#define CRIMSON_PROFILE_SCOPE(name) ZoneScopedN(name)
#define CRIMSON_PROFILE_FRAME() FrameMark
#define CRIMSON_PROFILE_PLOT(name, value) TracyPlot(name, static_cast<int64_t>(value))