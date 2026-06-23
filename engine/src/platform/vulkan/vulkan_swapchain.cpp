#include "vulkan_swapchain.hpp"


namespace crimson::vulkan
{
    VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, VkExtent2D extent)
        : m_device(device), m_surface(surface)
    {
        CreateSwapchain(extent);
        CreateCommandPool();
        CreateSyncObjects();
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        Cleanup();
        vkDestroyCommandPool(m_device.GetDevice(), m_commandPool, nullptr);
    }

    void VulkanSwapchain::CreateSwapchain(VkExtent2D extent, VkSwapchainKHR old)
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.GetPhysicalDevice(), m_surface, &capabilities);

        if (capabilities.currentExtent.width != UINT32_MAX)
            m_extent = capabilities.currentExtent;
        else
        {
            m_extent.width  = std::max(capabilities.minImageExtent.width,  std::min(capabilities.maxImageExtent.width,  extent.width));
            m_extent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, extent.height));
        }

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.GetPhysicalDevice(), m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.GetPhysicalDevice(), m_surface, &formatCount, formats.data());

        VkSurfaceFormatKHR chosenFormat = formats[0];
        for (const auto& f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosenFormat = f;
                break;
            }
        }
        m_format = chosenFormat.format;

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR swapInfo{};
        swapInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapInfo.surface          = m_surface;
        swapInfo.minImageCount    = imageCount;
        swapInfo.imageFormat      = chosenFormat.format;
        swapInfo.imageColorSpace  = chosenFormat.colorSpace;
        swapInfo.imageExtent      = m_extent;
        swapInfo.imageArrayLayers = 1;
        swapInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swapInfo.preTransform     = capabilities.currentTransform;
        swapInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        swapInfo.clipped          = VK_TRUE;
        swapInfo.oldSwapchain     = old;
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateSwapchainKHR(m_device.GetDevice(), &swapInfo, nullptr, &m_swapchain) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer]: Failed to create swapchain!");
            return;
        }

        uint32_t swapImageCount = 0;
        vkGetSwapchainImagesKHR(m_device.GetDevice(), m_swapchain, &swapImageCount, nullptr);
        m_images.resize(swapImageCount);
        vkGetSwapchainImagesKHR(m_device.GetDevice(), m_swapchain, &swapImageCount, m_images.data());
    }

    void VulkanSwapchain::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamilyIdx();
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(m_device.GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer]: Failed to create command pool!");
            return;
        }
    }

    void VulkanSwapchain::CreateSyncObjects()
    {
        const uint32_t imageCount = GetImageCount();
        m_frames.resize(imageCount);

        std::vector<VkCommandBuffer> commandBuffers(imageCount);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = imageCount;

        if (vkAllocateCommandBuffers(m_device.GetDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer]: Failed to allocate command buffers!");
            return;
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < imageCount; i++)
        {
            m_frames[i].CommandBuffer = commandBuffers[i];
            vkCreateSemaphore(m_device.GetDevice(), &semInfo, nullptr, &m_frames[i].RenderFinished);
            vkCreateFence(m_device.GetDevice(), &fenceInfo, nullptr, &m_frames[i].InFlight);
        }

        // N+1 acquire semaphores ??
        m_imageAvailableSemaphores.resize(imageCount + 1);
        for (auto& sem : m_imageAvailableSemaphores)
        {
            vkCreateSemaphore(m_device.GetDevice(), &semInfo, nullptr, &sem);
        }
    }

    void VulkanSwapchain::Cleanup()
    {
        for (const auto& frame : m_frames)
        {
            vkDestroySemaphore(m_device.GetDevice(), frame.RenderFinished, nullptr);
            vkDestroyFence(m_device.GetDevice(), frame.InFlight, nullptr);
        }

        m_frames.clear();

        for (const auto& sem : m_imageAvailableSemaphores)
            vkDestroySemaphore(m_device.GetDevice(), sem, nullptr);

        m_imageAvailableSemaphores.clear();

        if (m_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_device.GetDevice(), m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanSwapchain::Recreate(VkExtent2D newExtent)
    {
        vkDeviceWaitIdle(m_device.GetDevice());
        VkSwapchainKHR old = m_swapchain;

        // Cleanup sync objects but keep command pool
        for (const auto& frame : m_frames)
        {
            vkDestroySemaphore(m_device.GetDevice(), frame.RenderFinished, nullptr);
            vkDestroyFence(m_device.GetDevice(), frame.InFlight, nullptr);
        }

        m_frames.clear();

        for (const auto& sem : m_imageAvailableSemaphores)
        {
            vkDestroySemaphore(m_device.GetDevice(), sem, nullptr);
        }

        m_imageAvailableSemaphores.clear();
        m_images.clear();
        m_acquireSemaphoreIndex = 0;

        CreateSwapchain(newExtent, old);
        vkDestroySwapchainKHR(m_device.GetDevice(), old, nullptr); // Destroy old swapchain now that new one is created
        CreateSyncObjects();
    }

    VkResult VulkanSwapchain::AcquireNextImage(uint32_t& outImageIndex, VkSemaphore signalSemaphore) const
    {
        return vkAcquireNextImageKHR(m_device.GetDevice(), m_swapchain, UINT64_MAX,
                                     signalSemaphore, VK_NULL_HANDLE, &outImageIndex);
    }

    VkResult VulkanSwapchain::Present(uint32_t imageIndex, VkSemaphore waitSemaphore) const
    {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &waitSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &m_swapchain;
        presentInfo.pImageIndices      = &imageIndex;

        return vkQueuePresentKHR(m_device.GetGraphicsQueue(), &presentInfo);
    }
}
