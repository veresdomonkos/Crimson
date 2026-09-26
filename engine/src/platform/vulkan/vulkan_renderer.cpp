#include "vulkan_renderer.hpp"
#include "crimson/renderer/renderer.hpp"
#include <GLFW/glfw3.h>
#include "crimson/core/log.hpp"
#include <cstring>

namespace crimson::vulkan
{
    RenderSurfaceHandle VulkanRenderer::Initialize(const Window& primaryWindow)
    {
        m_device.Init();
        m_resourceManager.Init();
        InitializeSynchronizationAndCommands();
        InitGlobals();

        return m_resourceManager.CreateRenderSurface(primaryWindow);
    }

    void VulkanRenderer::InitializeSynchronizationAndCommands()
    {
        // --- VÁLTOZATLAN a korábbi kódhoz képest ---
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamilyIdx();

        if (vkCreateCommandPool(m_device.GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
            LOG_ERROR("[Renderer] Failed to create VkCommandPool!");

        std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> buffers{};

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        if (vkAllocateCommandBuffers(m_device.GetDevice(), &allocInfo, buffers.data()) != VK_SUCCESS)
            LOG_ERROR("[Renderer] Failed to allocate VkCommandBuffers!");

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_frames[i].SetIndex(i);
            m_frameSyncs[i].CommandBuffer = buffers[i];

            if (vkCreateSemaphore(m_device.GetDevice(), &semaphoreInfo, nullptr, &m_frameSyncs[i].ImageAvailableSemaphore) != VK_SUCCESS)
                LOG_ERROR("[Renderer] Failed to create Vulkan Semaphores!");

            if (vkCreateFence(m_device.GetDevice(), &fenceInfo, nullptr, &m_frameSyncs[i].InFlightFence) != VK_SUCCESS)
                LOG_ERROR("[Renderer] Failed to create Vulkan Fences!");
        }
    }

    void VulkanRenderer::InitGlobals()
    {
        if (m_resourceManager.GetDescriptorPool() == VK_NULL_HANDLE || m_resourceManager.GetGlobalSetLayout() == VK_NULL_HANDLE)
        {
            LOG_ERROR("[Vulkan] Cannot initialize global resources without descriptor pool or layout");
            return;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_device.GetPhysicalDevice(), &properties);
        VkDeviceSize minUboAlignment = properties.limits.minUniformBufferOffsetAlignment;

        // --- CameraBlock: per-pass, MAX_PASSES_PER_FRAME * MAX_FRAMES_IN_FLIGHT slot ---
        m_cameraUboAlignment = (sizeof(CameraBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);
        constexpr uint32_t MAX_PASSES_PER_FRAME = 16;
        VkDeviceSize cameraBufferSize = m_cameraUboAlignment * MAX_PASSES_PER_FRAME * MAX_FRAMES_IN_FLIGHT;

        m_resourceManager.CreateBuffer(cameraBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_cameraUBOBuffer, m_cameraUBOBufferMemory);
        vkMapMemory(m_device.GetDevice(), m_cameraUBOBufferMemory, 0, cameraBufferSize, 0, &m_cameraMappedData);

        // --- LightingBlock: per-frame (nem per-pass), csak MAX_FRAMES_IN_FLIGHT slot ---
        m_lightingUboAlignment = (sizeof(LightingBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);
        VkDeviceSize lightingBufferSize = m_lightingUboAlignment * MAX_FRAMES_IN_FLIGHT;

        m_resourceManager.CreateBuffer(lightingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_lightingUBOBuffer, m_lightingUBOBufferMemory);
        vkMapMemory(m_device.GetDevice(), m_lightingUBOBufferMemory, 0, lightingBufferSize, 0, &m_lightingMappedData);

        // --- Globális descriptor set ---
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_resourceManager.GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        VkDescriptorSetLayout globalLayout = m_resourceManager.GetGlobalSetLayout();
        allocInfo.pSetLayouts = &globalLayout;

        if (vkAllocateDescriptorSets(m_device.GetDevice(), &allocInfo, &m_globalDescriptorSet) != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to allocate global descriptor set!");
            return;
        }

        VkDescriptorBufferInfo cameraBufferInfo{ m_cameraUBOBuffer, 0, sizeof(CameraBlock) };
        VkWriteDescriptorSet cameraWrite{};
        cameraWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cameraWrite.dstSet = m_globalDescriptorSet;
        cameraWrite.dstBinding = kCameraBlockBinding;
        cameraWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        cameraWrite.descriptorCount = 1;
        cameraWrite.pBufferInfo = &cameraBufferInfo;

        VkDescriptorBufferInfo lightingBufferInfo{ m_lightingUBOBuffer, 0, sizeof(LightingBlock) };
        VkWriteDescriptorSet lightingWrite{};
        lightingWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightingWrite.dstSet = m_globalDescriptorSet;
        lightingWrite.dstBinding = kLightingBlockBinding;
        lightingWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        lightingWrite.descriptorCount = 1;
        lightingWrite.pBufferInfo = &lightingBufferInfo;

        VkWriteDescriptorSet writes[] = { cameraWrite, lightingWrite };
        vkUpdateDescriptorSets(m_device.GetDevice(), 2, writes, 0, nullptr);

        // ShadowMap (binding=2) write later, in SetShadowMap().
    }

    void VulkanRenderer::SetShadowMap(TextureHandle shadowMap)
    {
        const VulkanTexture& tex = m_resourceManager.GetTexture(shadowMap);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_resourceManager.GetDefaultSampler();
        imageInfo.imageView = tex.View;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_globalDescriptorSet;
        write.dstBinding = kShadowMapBinding;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device.GetDevice(), 1, &write, 0, nullptr);
    }

    void VulkanRenderer::Shutdown()
    {
        vkDeviceWaitIdle(m_device.GetDevice());

        if (m_globalDescriptorSet != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(m_device.GetDevice(), m_resourceManager.GetDescriptorPool(), 1, &m_globalDescriptorSet);
            m_globalDescriptorSet = VK_NULL_HANDLE;
        }

        auto destroyMapped = [&](VkBuffer& buf, VkDeviceMemory& mem, void*& mapped)
        {
            if (buf != VK_NULL_HANDLE)
            {
                if (mapped != nullptr) { vkUnmapMemory(m_device.GetDevice(), mem); mapped = nullptr; }
                vkDestroyBuffer(m_device.GetDevice(), buf, nullptr);
                vkFreeMemory(m_device.GetDevice(), mem, nullptr);
                buf = VK_NULL_HANDLE;
            }
        };

        destroyMapped(m_cameraUBOBuffer, m_cameraUBOBufferMemory, m_cameraMappedData);
        destroyMapped(m_lightingUBOBuffer, m_lightingUBOBufferMemory, m_lightingMappedData);

        m_resourceManager.Clear();

        for (FrameSync& sync : m_frameSyncs)
        {
            vkDestroySemaphore(m_device.GetDevice(), sync.ImageAvailableSemaphore, nullptr);
            vkDestroyFence(m_device.GetDevice(), sync.InFlightFence, nullptr);
        }

        if (m_commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_device.GetDevice(), m_commandPool, nullptr);

        m_device.Shutdown();
    }

    ResourceManager& VulkanRenderer::GetResourceManager() { return m_resourceManager; }

    void VulkanRenderer::TransitionImage(VkCommandBuffer cmd, VulkanTexture& texture, VkImageAspectFlagBits flagBits, VkImageLayout newLayout)
    {
        // --- VÁLTOZATLAN a korábbi körhöz képest ---
        if (texture.Layout == newLayout) return;

        VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE, dstStage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE, dstAccess = VK_ACCESS_2_NONE;

        if (texture.Layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else if (texture.Layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else if (texture.Layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        if (flagBits == VK_IMAGE_ASPECT_COLOR_BIT)
        {
            if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) { dstStage = VK_PIPELINE_STAGE_2_NONE; dstAccess = VK_ACCESS_2_NONE; }
            else { dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; }
        }
        else
        {
            dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = texture.Layout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.Image;
        barrier.subresourceRange = { static_cast<VkImageAspectFlags>(flagBits), 0, 1, 0, 1 };
        barrier.srcStageMask = srcStage; barrier.dstStageMask = dstStage;
        barrier.srcAccessMask = srcAccess; barrier.dstAccessMask = dstAccess;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dep);
        texture.Layout = newLayout;
    }

    void VulkanRenderer::ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const RenderPassInfo& info, uint32_t passIndex)
    {
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(info.Target);

        // CameraBlock - per-pass írás
        if (m_cameraMappedData != nullptr)
        {
            CameraBlock block{};
            block.ViewProj = info.ViewProj;
            block.Position = glm::vec4(info.CameraPosition, 0.0f);

            uint32_t globalPassIndex = (m_currentFrameIndex * 16) + passIndex;
            VkDeviceSize offset = globalPassIndex * m_cameraUboAlignment;
            std::memcpy(static_cast<char*>(m_cameraMappedData) + offset, &block, sizeof(CameraBlock));
        }

        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(rt.ColorAttachments.size());

        for (TextureHandle colorHandle : rt.ColorAttachments)
        {
            VulkanTexture& color = m_resourceManager.GetTexture(colorHandle);
            TransitionImage(cmdBuffer, color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            VkRenderingAttachmentInfo attachment{};
            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = color.View;
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.clearValue.color = { info.ClearColor.r, info.ClearColor.g, info.ClearColor.b, info.ClearColor.a };
            colorAttachments.push_back(attachment);
        }

        VkRenderingAttachmentInfo depthAttachment{};
        bool hasDepth = rt.DepthAttachment.has_value();

        if (hasDepth)
        {
            VulkanTexture& depth = m_resourceManager.GetTexture(*rt.DepthAttachment);
            TransitionImage(cmdBuffer, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depth.View;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = { info.ClearDepth, info.ClearStencil };
        }

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea = { {0, 0}, {rt.Width, rt.Height} };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        rendering.pColorAttachments = colorAttachments.data();
        if (hasDepth) rendering.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmdBuffer, &rendering);

        VkViewport viewport{ 0.0f, static_cast<float>(rt.Height), static_cast<float>(rt.Width), -static_cast<float>(rt.Height), 0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, {rt.Width, rt.Height} };
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
    }

    void VulkanRenderer::ExecuteEndRenderPass(VkCommandBuffer cmdBuffer, VulkanRenderTarget& rt)
    {
        vkCmdEndRendering(cmdBuffer);

        for (TextureHandle colorHandle : rt.ColorAttachments)
        {
            VulkanTexture& color = m_resourceManager.GetTexture(colorHandle);
            const VkImageLayout targetLayout = rt.IsSwapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            TransitionImage(cmdBuffer, color, VK_IMAGE_ASPECT_COLOR_BIT, targetLayout);
        }

        if (rt.DepthAttachment && !rt.IsSwapchain)
        {
            VulkanTexture& depth = m_resourceManager.GetTexture(*rt.DepthAttachment);
            TransitionImage(cmdBuffer, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void VulkanRenderer::ExecuteDraw(VkCommandBuffer cmdBuffer, const DrawInfo& draw, RenderTargetHandle target, uint32_t passIndex)
    {
        VulkanVertexBuffer& vertexBuffer = m_resourceManager.GetVertexBuffer(draw.VertexBuffer);
        VulkanIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(draw.IndexBuffer);
        VulkanMaterial& material = m_resourceManager.GetMaterial(draw.Material);

        if (material.DescriptorSet == VK_NULL_HANDLE) return;

        VulkanGraphicsPipeline& pipeline = m_resourceManager.GetOrCreateGraphicsPipeline({
            .Layout = vertexBuffer.Layout, .Shader = material.Shader, .Target = target
        });
        if (pipeline.Pipeline == VK_NULL_HANDLE) return;

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Pipeline);

        VkDeviceSize vbOffset = 0;
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.Buffer, &vbOffset);
        vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.Buffer, 0, indexBuffer.Type == IndexType::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);

        uint32_t globalPassIndex = (m_currentFrameIndex * 16) + passIndex;

        // Sorrend a pipeline layout binding-sorrendjét követi: binding=0 (Camera) majd binding=1 (Lighting)
        uint32_t dynamicOffsets[2] = {
            static_cast<uint32_t>(globalPassIndex * m_cameraUboAlignment),
            static_cast<uint32_t>(m_currentFrameIndex * m_lightingUboAlignment)
        };

        VkDescriptorSet descriptorSets[] = { m_globalDescriptorSet, material.DescriptorSet };
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, 0, 2, descriptorSets, 2, dynamicOffsets);

        uint32_t indexCount = static_cast<uint32_t>(indexBuffer.Size / Index::Size(indexBuffer.Type));
        vkCmdDrawIndexed(cmdBuffer, indexCount, 1, 0, 0, 0);
    }

    FrameContext VulkanRenderer::BeginFrame(RenderSurfaceHandle surfaceHandle, const FrameLightingData& lighting)
    {
        m_frames[m_currentFrameIndex].Reset();

        VulkanSurface& surface = m_resourceManager.GetRenderSurface(surfaceHandle);
        VkFence frameFence = m_frameSyncs[m_currentFrameIndex].InFlightFence;

        vkWaitForFences(m_device.GetDevice(), 1, &frameFence, VK_TRUE, UINT64_MAX);

        // A LightingBlock írása MOST, közvetlenül a wait-fences UTÁN - garantáltan
        // nincs GPU által még olvasott memóriába írás (a fence bevárta az előző
        // használatot ugyanezen frame-slot esetén).
        if (m_lightingMappedData != nullptr)
        {
            LightingBlock block{};
            block.AmbientColor = glm::vec4(lighting.AmbientColor, 0.0f);
            block.ShadowViewProj = lighting.ShadowViewProj;
            block.ShadowLightIndex = lighting.ShadowLightIndex;
            block.LightCount = std::min<uint32_t>(static_cast<uint32_t>(lighting.Lights.size()), kMaxLights);

            for (uint32_t i = 0; i < block.LightCount; ++i)
                block.Lights[i] = lighting.Lights[i].ToGPULight();

            VkDeviceSize offset = m_currentFrameIndex * m_lightingUboAlignment;
            std::memcpy(static_cast<char*>(m_lightingMappedData) + offset, &block, sizeof(LightingBlock));
        }

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_device.GetDevice(), surface.Swapchain, UINT64_MAX,
            m_frameSyncs[m_currentFrameIndex].ImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_resourceManager.RecreateSwapchain(surfaceHandle);
            return m_frames[m_currentFrameIndex].CreateContext();
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            LOG_ERROR("[Renderer] AcquireNextImage failed");

        surface.CurrentImageIndex = imageIndex;
        if (surface.ImagesInFlight[imageIndex] != VK_NULL_HANDLE)
            vkWaitForFences(m_device.GetDevice(), 1, &surface.ImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);

        surface.ImagesInFlight[imageIndex] = frameFence;
        vkResetFences(m_device.GetDevice(), 1, &frameFence);

        VkCommandBuffer cmd = m_frameSyncs[m_currentFrameIndex].CommandBuffer;
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        m_frames[m_currentFrameIndex].Init(surfaceHandle, m_resourceManager.GetCurrentBackBuffer(surfaceHandle), true);

        return m_frames[m_currentFrameIndex].CreateContext();
    }

    void VulkanRenderer::EndFrame(const FrameContext& frameContext)
    {
        Frame& frame = m_frames[frameContext.GetIndex()];
        VkCommandBuffer cmdBuffer = m_frameSyncs[frameContext.GetIndex()].CommandBuffer;

        uint32_t passIndex = 0;
        for (const auto& renderPass : frame.GetRenderPasses())
        {
            const RenderPassInfo& passInfo = renderPass.Info();
            VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(passInfo.Target);

            ExecuteBeginRenderPass(cmdBuffer, passInfo, passIndex);
            for (const auto& draw : renderPass.GetDraws())
                ExecuteDraw(cmdBuffer, draw, passInfo.Target, passIndex);
            ExecuteEndRenderPass(cmdBuffer, rt);
            ++passIndex;
        }

        if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS)
            LOG_ERROR("[Renderer] Failed to end command buffer");

        VulkanSurface& surface = m_resourceManager.GetRenderSurface(frame.GetSurface());
        uint32_t imageIndex = surface.CurrentImageIndex;

        VkSemaphore waitSemaphore = m_frameSyncs[frameContext.GetIndex()].ImageAvailableSemaphore;
        VkSemaphore signalSemaphore = surface.RenderFinishedSemaphores[imageIndex];
        VkFence fence = m_frameSyncs[frameContext.GetIndex()].InFlightFence;
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

        if (vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submit, fence) != VK_SUCCESS)
            LOG_ERROR("Queue submit failed");

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &signalSemaphore;
        present.swapchainCount = 1;
        present.pSwapchains = &surface.Swapchain;
        present.pImageIndices = &imageIndex;

        VkResult result = vkQueuePresentKHR(m_device.GetGraphicsQueue(), &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            m_resourceManager.RecreateSwapchain(frame.GetSurface());

        m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}