#pragma once

#include <memory>

#include "crimson/core/window.hpp"
#include "crimson/graphics/graphics_api.hpp"
#include "crimson/graphics/graphics_backend.hpp"
#include "../platform/window_graphics.hpp"

namespace crimson
{
    // Makes the graphics backend for a window. Every platform's window calls this from its CreateGraphicsBackend(),
    // passing itself as both arguments, so the choice between the APIs is made in one place.
    std::unique_ptr<GraphicsBackend> CreateGraphicsBackend(GraphicsApi api, Window& window, WindowGraphics& graphics);
}
