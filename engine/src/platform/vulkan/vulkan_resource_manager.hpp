#pragma once

#include <vulkan/vulkan.h>

#include "vulkan_swapchain.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson::vulkan
{
    struct VulkanSurface
    {
        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        VkSemaphore CurrentAcquireSemaphore = VK_NULL_HANDLE;
        std::unique_ptr<VulkanSwapchain> Swapchain;
        uint32_t CurrentImageIndex = 0;
        uint32_t CurrentFrameIndex = 0;
    };

    class VulkanResourceManager: public ResourceManager
    {
    public:
        explicit VulkanResourceManager(VulkanDevice& device) : m_device(device) {}
        SurfaceHandle CreateSurface(const Window& window) override;

        [[nodiscard]] std::unordered_map<SurfaceHandle, VulkanSurface>& GetSurfaces() { return m_surfaces; }
        [[nodiscard]] const std::unordered_map<SurfaceHandle, VulkanSurface>& GetSurfaces() const { return m_surfaces; }

    private:
        VulkanDevice& m_device;
        std::unordered_map<SurfaceHandle, VulkanSurface> m_surfaces;
    };
}
