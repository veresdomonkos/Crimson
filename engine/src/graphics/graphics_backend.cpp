// graphics_backend_vulkan.cpp

#include "crimson/graphics/graphics_backend.hpp"
#include "../platform/vulkan/vulkan_device.hpp"
#include "../platform/vulkan/vulkan_resource_manager.hpp"
#include "../platform/vulkan/vulkan_renderer.hpp"
#include "../platform/vulkan/vulkan_imgui_backend.hpp"
#include "crimson/renderer/graphics_api.hpp"

#include "../platform/opengl/opengl_device.hpp"
#include "../platform/opengl/opengl_resource_manager.hpp"
#include "../platform/opengl/opengl_renderer.hpp"
#include "../platform/opengl/opengl_imgui_backend.hpp"

namespace crimson
{
    static GraphicsBackend CreateVulkanBackend(Window& window)
    {
        GraphicsBackend backend;

        auto device = std::make_unique<vulkan::VulkanDevice>(window);
        auto resourceManager = std::make_unique<vulkan::VulkanResourceManager>(*device);
        auto renderer = std::make_unique<vulkan::VulkanRenderer>(*device, *resourceManager);
        std::unique_ptr<ImGuiBackend> imgui = std::make_unique<vulkan::VulkanImGuiBackend>(*device, *resourceManager, window);

        backend.Device = std::move(device);
        backend.GPUResources = std::move(resourceManager);
        backend.Renderer = std::move(renderer);
        backend.Imgui = std::move(imgui);

        return backend;
    }

    static GraphicsBackend CreateOpenGLBackend(Window& window)
    {
        GraphicsBackend backend;

        auto device = std::make_unique<opengl::OpenGLDevice>(window);
        auto resourceManager = std::make_unique<opengl::OpenGLResourceManager>(window);
        auto renderer = std::make_unique<opengl::OpenGLRenderer>(*device, *resourceManager);
        std::unique_ptr<ImGuiBackend> imgui = std::make_unique<opengl::OpenGLImGuiBackend>(*device, *resourceManager);

        backend.Device = std::move(device);
        backend.GPUResources = std::move(resourceManager);
        backend.Renderer = std::move(renderer);
        backend.Imgui = std::move(imgui);

        return backend;
    }


    std::unique_ptr<GraphicsBackend> GraphicsBackend::Create(GraphicsApi type, Window& window)
    {
        switch (type)
        {
            case GraphicsApi::Vulkan: return std::make_unique<GraphicsBackend>(CreateVulkanBackend(window));
            case GraphicsApi::OpenGL: return std::make_unique<GraphicsBackend>(CreateOpenGLBackend(window));
        }
        return {};
    }
}
