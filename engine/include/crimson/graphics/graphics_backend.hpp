#pragma once

#include <memory>

#include "graphics_device.hpp"
#include "crimson/graphics/graphics_api.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    // What a window's graphics API gives it: made by Window::CreateGraphicsBackend()
    struct GraphicsBackend
    {
        std::unique_ptr<GraphicsDevice> Device;
        std::unique_ptr<GpuResourceManager> GPUResources;
        std::unique_ptr<crimson::Renderer> Renderer;
    };
}
