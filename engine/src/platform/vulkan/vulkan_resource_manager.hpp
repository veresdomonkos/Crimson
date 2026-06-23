#pragma once

#include <vulkan/vulkan.h>

#include "vulkan_swapchain.hpp"
#include "crimson/renderer/handle_registry.hpp"
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
        RenderSurfaceHandle CreateRenderSurface(const Window& window) override;
        RenderTargetHandle GetBackBuffer(RenderSurfaceHandle renderSurface) override { return RenderTargetHandle::Invalid(); };
        VulkanSurface& GetRenderSurface(RenderSurfaceHandle handle) { return m_renderSurfaces.Get(handle);  }
    private:
        VulkanDevice& m_device;
        HandleRegistry<RenderSurfaceHandle, VulkanSurface> m_renderSurfaces;
    };
}
