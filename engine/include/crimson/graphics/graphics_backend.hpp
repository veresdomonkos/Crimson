#pragma once

#include <memory>

#include "graphics_device.hpp"
#include "crimson/renderer/graphics_api.hpp"
#include "crimson/ui/imgui_backend.hpp"

namespace crimson
{
    struct GraphicsBackend
    {
        std::unique_ptr<GraphicsDevice> Device;
        std::unique_ptr<GpuResourceManager> GPUResources;
        std::unique_ptr<crimson::Renderer> Renderer;
        std::unique_ptr<ImGuiBackend> Imgui;

        static std::unique_ptr<GraphicsBackend> Create(GraphicsApi type, Window& window);
    };
}
