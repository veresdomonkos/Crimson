#include "vulkan_renderer.hpp"

#include "crimson/renderer/renderer.hpp"
#include <GLFW/glfw3.h>

namespace crimson::vulkan
{
    SurfaceHandle VulkanRenderer::Initialize(const Window& primaryWindow)
    {
        m_device.Init();
        return m_resourceManager.CreateSurface(primaryWindow);
    }

    void VulkanRenderer::Shutdown()
    {
        vkDeviceWaitIdle(m_device.GetDevice());

        for (auto& [handle, surface] : m_resourceManager.GetSurfaces())
        {
            surface.Swapchain.reset(); // destroys swapchain + sync objects
            vkDestroySurfaceKHR(m_device.GetInstance(), surface.Surface, nullptr);
        }

        m_resourceManager.GetSurfaces().clear();
        m_device.Shutdown();
    }

    ResourceManager& VulkanRenderer::GetResourceManager()
    {
        return m_resourceManager;
    }

    Frame VulkanRenderer::BeginFrame(SurfaceHandle surface)
    {
        auto& vulkanSurface      = m_resourceManager.GetSurfaces()[surface];
        auto& swapchain    = *vulkanSurface.Swapchain;
        const uint32_t frameIdx = vulkanSurface.CurrentFrameIndex;

        VkFence fence = swapchain.GetInFlightFence(frameIdx);
        vkWaitForFences(m_device.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_device.GetDevice(), 1, &fence);

        VkSemaphore acquireSem = swapchain.GetCurrentAcquireSemaphore();
        swapchain.AdvanceAcquireSemaphore();

        VkResult result = swapchain.AcquireNextImage(vulkanSurface.CurrentImageIndex, acquireSem);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            // TODO: get actual new extent from window
            vkDeviceWaitIdle(m_device.GetDevice());
            return Frame(surface, 2 * 1024 * 1024);
        }

        vulkanSurface.CurrentAcquireSemaphore = acquireSem;

        VkCommandBuffer cmd = swapchain.GetCommandBuffer(frameIdx);
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImageMemoryBarrier barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = swapchain.GetImage(vulkanSurface.CurrentImageIndex);
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask               = 0;
        barrier.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        return Frame(surface, 2 * 1024 * 1024);
    }

    void VulkanRenderer::EndFrame(Frame& frame)
    {
        auto& surface = m_resourceManager.GetSurfaces()[frame.GetSurfaceHandle()];
        const auto& swapchain = *surface.Swapchain;
        const uint32_t imageIndex = surface.CurrentImageIndex;
        const uint32_t frameIdx = surface.CurrentFrameIndex;
        VkCommandBuffer cmd = swapchain.GetCommandBuffer(frameIdx);

        glm::vec4 clearColor{0, 0, 0, 1};
        for (const auto& command : frame.GetCommandQueue())
        {
            if (command.GetType() == RendererCommandType::Clear)
            {
                clearColor = command.As<ClearCommand>().Color;
                break;
            }
        }

        VkClearColorValue clearValue{{ clearColor.r, clearColor.g, clearColor.b, clearColor.a }};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;

        vkCmdClearColorImage(cmd, swapchain.GetImage(imageIndex),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = swapchain.GetImage(imageIndex);
        barrier.subresourceRange    = range;
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = 0;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr,
            1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSemaphore waitSem = surface.CurrentAcquireSemaphore;
        VkSemaphore signalSem = swapchain.GetRenderFinished(frameIdx);
        VkFence fence = swapchain.GetInFlightFence(frameIdx);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &waitSem;
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &signalSem;

        vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submitInfo, fence);

        VkResult result = swapchain.Present(imageIndex, signalSem);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            vkDeviceWaitIdle(m_device.GetDevice()); // TODO: proper recreate
        }

        surface.CurrentFrameIndex = (frameIdx + 1) % swapchain.GetImageCount();
    }
}
