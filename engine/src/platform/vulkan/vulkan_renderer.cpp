#include "vulkan_renderer.hpp"

#include "crimson/renderer/renderer.hpp"
#include <GLFW/glfw3.h>

namespace crimson::vulkan
{
    RenderSurfaceHandle VulkanRenderer::Initialize(const Window& primaryWindow)
    {
        m_device.Init();
        InitializeSynchronizationAndCommands();
        return m_resourceManager.CreateRenderSurface(primaryWindow);
    }

    void VulkanRenderer::InitializeSynchronizationAndCommands()
    {
        m_vkCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamilyIdx();

        if (vkCreateCommandPool(m_device.GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create VkCommandPool!");
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        if (vkAllocateCommandBuffers(m_device.GetDevice(), &allocInfo, m_vkCommandBuffers.data()) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to allocate VkCommandBuffers!");
        }

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vkCreateSemaphore(m_device.GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS)
            {
                LOG_ERROR("[Renderer] Failed to create Vulkan Semaphores!");
            }

            if (vkCreateFence(m_device.GetDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
            {
                LOG_ERROR("[Renderer] Failed to create Vulkan Fences!");
            }
        }
    }

    void VulkanRenderer::Shutdown()
    {
        vkDeviceWaitIdle(m_device.GetDevice());

        m_resourceManager.Clear();

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroySemaphore(m_device.GetDevice(), m_imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_device.GetDevice(), m_inFlightFences[i], nullptr);
        }

        if (m_commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_device.GetDevice(), m_commandPool, nullptr);
        }

        m_device.Shutdown();
    }

    ResourceManager& VulkanRenderer::GetResourceManager()
    {
        return m_resourceManager;
    }

    void VulkanRenderer::TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlagBits flagBits, VkImageLayout& currentLayout, VkImageLayout newLayout)
    {
        if (currentLayout == newLayout)
            return;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

        barrier.oldLayout = currentLayout;
        barrier.newLayout = newLayout;

        barrier.srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;

        barrier.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;

        barrier.image = image;

        barrier.subresourceRange.aspectMask =
            flagBits;

        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        barrier.srcAccessMask =
            VK_ACCESS_NONE;

        barrier.dstAccessMask =
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        VkDependencyInfo dep{};
        dep.sType =
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO;

        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dep);

        currentLayout = newLayout;
    }

    void VulkanRenderer::ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const BeginRenderPassCommand& cmd)
    {
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(cmd.Target);

        for (auto& color : rt.Colors)
        {
            TransitionImage(cmdBuffer, color.Image, VK_IMAGE_ASPECT_COLOR_BIT, color.Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        if (rt.Depth)
        {
            TransitionImage(cmdBuffer, rt.Depth->Image, VK_IMAGE_ASPECT_DEPTH_BIT, rt.Depth->Layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }

        std::vector<VkRenderingAttachmentInfo> colorAttachments;

        for(auto& color : rt.Colors)
        {
            VkRenderingAttachmentInfo attachment{};

            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = color.View;
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkClearValue clear{};
            clear.color = {
                cmd.ClearColor.r,
                cmd.ClearColor.g,
                cmd.ClearColor.b,
                cmd.ClearColor.a
            };

            attachment.clearValue = clear;
            colorAttachments.push_back(attachment);
        }

        VkRenderingAttachmentInfo depthAttachment{};

        if(rt.Depth)
        {
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = rt.Depth->View;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil =
            {
                cmd.ClearDepth,
                cmd.ClearStencil
            };
        }

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.offset = {0,0};
        rendering.renderArea.extent = {rt.Width, rt.Height};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());;
        rendering.pColorAttachments = colorAttachments.data();
        if(rt.Depth)
        {
            rendering.pDepthAttachment = &depthAttachment;
        }

        vkCmdBeginRendering(cmdBuffer, &rendering);
    }

    void VulkanRenderer::ExecuteEndRenderPass(VkCommandBuffer cmdBuffer, VulkanRenderTarget& rt)
    {
        vkCmdEndRendering(cmdBuffer);

        if (rt.IsSwapchain)
        {
            for (auto& color : rt.Colors)
            {
                TransitionImage(cmdBuffer, color.Image, VK_IMAGE_ASPECT_COLOR_BIT, color.Layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            }
        }
    }

    /*
    Frame VulkanRenderer::BeginFrame(RenderSurfaceHandle surface)
    {
        auto& vulkanSurface      = m_resourceManager.GetRenderSurface(surface);
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
            return Frame(surface, m_commandBuffer);
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

        m_commandBuffer.Clear();
        return Frame(surface, m_commandBuffer);
    }
    */
    /*
    void VulkanRenderer::EndFrame(Frame& frame)
    {
        auto& surface = m_resourceManager.GetRenderSurface(frame.GetSurfaceHandle());
        const auto& swapchain = *surface.Swapchain;
        const uint32_t imageIndex = surface.CurrentImageIndex;
        const uint32_t frameIdx = surface.CurrentFrameIndex;
        VkCommandBuffer cmd = swapchain.GetCommandBuffer(frameIdx);

        glm::vec4 clearColor{0, 0, 0, 1};
        for (const auto& command : frame.GetCommandBuffer())
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
    */

    std::optional<Frame> VulkanRenderer::BeginFrame(RenderSurfaceHandle surfaceHandle)
    {
        VulkanSurface& surface = m_resourceManager.GetRenderSurface(surfaceHandle);
        VkFence frameFence = m_inFlightFences[m_currentFrameIndex];

        vkWaitForFences(
            m_device.GetDevice(),
            1,
            &frameFence,
            VK_TRUE,
            UINT64_MAX
        );

        uint32_t imageIndex;

        VkResult result = vkAcquireNextImageKHR(
            m_device.GetDevice(),
            surface.Swapchain,
            UINT64_MAX,
            m_imageAvailableSemaphores[m_currentFrameIndex],
            VK_NULL_HANDLE,
            &imageIndex
        );

        if(result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_resourceManager.RecreateSwapchain(surfaceHandle);
            return std::nullopt;
        }


        if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            LOG_ERROR("[Renderer] AcquireNextImage failed");
        }

        surface.CurrentImageIndex = imageIndex;

        if(surface.ImagesInFlight[imageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(
                m_device.GetDevice(),
                1,
                &surface.ImagesInFlight[imageIndex],
                VK_TRUE,
                UINT64_MAX
            );
        }

        surface.ImagesInFlight[imageIndex] = frameFence;

        vkResetFences(
            m_device.GetDevice(),
            1,
            &frameFence
        );

        VkCommandBuffer cmd = m_vkCommandBuffers[m_currentFrameIndex];
        vkResetCommandBuffer(cmd,0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmd,&begin);
        m_commandBuffer.Clear();
        return Frame(surfaceHandle,m_commandBuffer);
    }

    void VulkanRenderer::EndFrame(Frame& frame)
    {
        VkCommandBuffer cmdBuffer = m_vkCommandBuffers[m_currentFrameIndex];
        VulkanSurface& surface = m_resourceManager.GetRenderSurface(frame.GetSurfaceHandle());
        uint32_t imageIndex = surface.CurrentImageIndex;
        RenderTargetHandle target = surface.SwapchainTargetHandles[imageIndex];
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(target);

        for (const auto& commandHeader : frame.GetCommandBuffer())
        {
            switch (commandHeader.GetType())
            {
                case RendererCommandType::BeginRenderPass:
                {
                    ExecuteBeginRenderPass(cmdBuffer, commandHeader.As<BeginRenderPassCommand>());
                    break;
                }

                case RendererCommandType::EndRenderPass:
                {
                    ExecuteEndRenderPass(cmdBuffer, rt);
                    break;
                }

                default:
                    break;
            }
        }

        if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS)
            LOG_ERROR("[Renderer] Failed to end command buffer");

        VkSemaphore waitSemaphore = m_imageAvailableSemaphores[m_currentFrameIndex];
        VkSemaphore signalSemaphore = surface.RenderFinishedSemaphores[imageIndex];
        VkFence fence = m_inFlightFences[m_currentFrameIndex];

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &waitSemaphore;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmdBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signalSemaphore;

        if (vkQueueSubmit(
            m_device.GetGraphicsQueue(),
            1,
            &submit,
            fence) != VK_SUCCESS)
        {
            LOG_ERROR("Queue submit failed");
        }

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &signalSemaphore;
        present.swapchainCount = 1;
        present.pSwapchains = &surface.Swapchain;
        present.pImageIndices = &imageIndex;

        VkResult result = vkQueuePresentKHR(
            m_device.GetGraphicsQueue(),
            &present
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            m_resourceManager.RecreateSwapchain(frame.GetSurfaceHandle());
        }

        m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}
