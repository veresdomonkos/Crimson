#include "vulkan_resource_manager.hpp"

#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "GLFW/glfw3.h"

namespace crimson::vulkan
{
    SurfaceHandle VulkanResourceManager::CreateSurface(const Window& window)
    {
        static SurfaceHandle id = 0;
        VulkanSurface data{};

        auto* glfwWindow = static_cast<GLFWwindow*>(window.GetNativeHandle());
        if (!glfwVulkanSupported())
        {
            LOG_ERROR("[Renderer]: GLFW version doesn't support Vulkan");
            return 0;
        }

        if (glfwCreateWindowSurface(m_device.GetInstance(), glfwWindow, nullptr, &data.Surface) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer]: Failed to create Vulkan surface!");
            return 0;
        }

        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_device.GetPhysicalDevice(),
            m_device.GetGraphicsQueueFamilyIdx(), data.Surface, &supported);

        if (!supported)
        {
            LOG_ERROR("[Renderer]: Graphics queue does not support present!");
            return 0;
        }

        // Get initial extent from the window
        int w, h;
        glfwGetFramebufferSize(glfwWindow, &w, &h);
        VkExtent2D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };

        data.Swapchain = std::make_unique<VulkanSwapchain>(m_device, data.Surface, extent);

        m_surfaces[++id] = std::move(data);
        return id;
    }
}
