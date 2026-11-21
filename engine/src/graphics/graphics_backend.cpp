// The one place that chooses between the graphics APIs

#include "create_graphics_backend.hpp"
#include "../platform/vulkan/vulkan_device.hpp"
#include "../platform/vulkan/vulkan_resource_manager.hpp"
#include "../platform/vulkan/vulkan_renderer.hpp"
#include "../platform/vulkan/vulkan_imgui_backend.hpp"
#include "crimson/graphics/graphics_api.hpp"

#include "../platform/opengl/opengl_device.hpp"
#include "../platform/opengl/opengl_resource_manager.hpp"
#include "../platform/opengl/opengl_renderer.hpp"
#include "../platform/opengl/opengl_imgui_backend.hpp"

namespace crimson
{
    static GraphicsBackend CreateVulkanBackend(Window& window, WindowGraphics& graphics)
    {
        GraphicsBackend backend;

        auto device = std::make_unique<vulkan::VulkanDevice>(window, graphics);
        auto resourceManager = std::make_unique<vulkan::VulkanResourceManager>(*device);
        auto renderer = std::make_unique<vulkan::VulkanRenderer>(*device, *resourceManager);
        std::unique_ptr<ImGuiBackend> imgui = std::make_unique<vulkan::VulkanImGuiBackend>(*device, *resourceManager, window);

        backend.Device = std::move(device);
        backend.GPUResources = std::move(resourceManager);
        backend.Renderer = std::move(renderer);
        backend.Imgui = std::move(imgui);

        return backend;
    }

    static GraphicsBackend CreateOpenGLBackend(Window& window, WindowGraphics& graphics)
    {
        GraphicsBackend backend;

        auto device = std::make_unique<opengl::OpenGLDevice>(graphics);
        auto resourceManager = std::make_unique<opengl::OpenGLResourceManager>(window);
        auto renderer = std::make_unique<opengl::OpenGLRenderer>(*device, *resourceManager);
        std::unique_ptr<ImGuiBackend> imgui = std::make_unique<opengl::OpenGLImGuiBackend>(*device, *resourceManager, window.GetNativeHandle());

        backend.Device = std::move(device);
        backend.GPUResources = std::move(resourceManager);
        backend.Renderer = std::move(renderer);
        backend.Imgui = std::move(imgui);

        return backend;
    }

    std::unique_ptr<GraphicsBackend> CreateGraphicsBackend(GraphicsApi api, Window& window, WindowGraphics& graphics)
    {
        switch (api)
        {
            case GraphicsApi::Vulkan: return std::make_unique<GraphicsBackend>(CreateVulkanBackend(window, graphics));
            case GraphicsApi::OpenGL: return std::make_unique<GraphicsBackend>(CreateOpenGLBackend(window, graphics));
        }
        return {};
    }
}
