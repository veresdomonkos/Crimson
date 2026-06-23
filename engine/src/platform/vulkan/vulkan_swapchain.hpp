#pragma once

#include <vector>
#include "vulkan_device.hpp"

namespace crimson::vulkan
{
    class VulkanSwapchain
    {
    public:
        VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, VkExtent2D extent);
        ~VulkanSwapchain();

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        [[nodiscard]] VkFormat GetFormat() const { return m_format; }
        [[nodiscard]] VkExtent2D GetExtent() const { return m_extent; }
        [[nodiscard]] uint32_t GetImageCount() const { return static_cast<uint32_t>(m_images.size()); }
        [[nodiscard]] VkImage GetImage(uint32_t index) const { return m_images[index]; }
        [[nodiscard]] VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_frames[frameIndex].CommandBuffer; }
        [[nodiscard]] VkSemaphore GetRenderFinished(uint32_t frameIndex) const { return m_frames[frameIndex].RenderFinished; }
        [[nodiscard]] VkFence GetInFlightFence(uint32_t frameIndex) const { return m_frames[frameIndex].InFlight; }
        [[nodiscard]] VkSemaphore GetCurrentAcquireSemaphore() const { return m_imageAvailableSemaphores[m_acquireSemaphoreIndex]; }

        void AdvanceAcquireSemaphore() { m_acquireSemaphoreIndex = (m_acquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_imageAvailableSemaphores.size()); }

        VkResult AcquireNextImage(uint32_t& outImageIndex, VkSemaphore signalSemaphore) const;
        VkResult Present(uint32_t imageIndex, VkSemaphore waitSemaphore) const;

        void Recreate(VkExtent2D newExtent);

    private:
        void CreateSwapchain(VkExtent2D extent, VkSwapchainKHR old = VK_NULL_HANDLE);
        void CreateCommandPool();
        void CreateSyncObjects();
        void Cleanup();

        struct FrameSync
        {
            VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
            VkSemaphore RenderFinished = VK_NULL_HANDLE;
            VkFence InFlight = VK_NULL_HANDLE;
        };

        VulkanDevice& m_device;
        VkSurfaceKHR m_surface;
        VkFormat m_format{};
        VkExtent2D m_extent{};
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        std::vector<VkImage> m_images;
        std::vector<FrameSync> m_frames;
        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        uint32_t m_acquireSemaphoreIndex = 0;
    };
}
