#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include <cstring>

namespace crimson::vulkan
{
    VulkanRenderer::VulkanRenderer(VulkanDevice& device, VulkanResourceManager& resourceManager)
        : m_device(device), m_resourceManager(resourceManager)
    {
        InitializeSynchronizationAndCommands();
        InitGlobals();
    }

    VulkanRenderer::~VulkanRenderer()
    {
        m_device.WaitIdle();

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

        for (FrameSync& sync : m_frameSyncs)
        {
            vkDestroySemaphore(m_device.GetDevice(), sync.ImageAvailableSemaphore, nullptr);
            vkDestroyFence(m_device.GetDevice(), sync.InFlightFence, nullptr);
        }

        if (m_commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_device.GetDevice(), m_commandPool, nullptr);
    }

    void VulkanRenderer::InitializeSynchronizationAndCommands()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamily();

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
        if (m_resourceManager.GetDescriptorPool() == VK_NULL_HANDLE ||
            m_resourceManager.GetGlobalSetLayout() == VK_NULL_HANDLE)
        {
            LOG_ERROR("[Vulkan] Cannot initialize global resources without descriptor pool or layout");
            return;
        }

        VkDeviceSize minUboAlignment =
            m_device.GetProperties().limits.minUniformBufferOffsetAlignment;

        m_cameraUboAlignment =
            (sizeof(CameraBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);

        constexpr uint32_t MAX_PASSES_PER_FRAME = 16;

        VkDeviceSize cameraBufferSize =
            m_cameraUboAlignment * MAX_PASSES_PER_FRAME * MAX_FRAMES_IN_FLIGHT;

        if (!m_resourceManager.CreateBuffer(
                cameraBufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_cameraUBOBuffer,
                m_cameraUBOBufferMemory))
        {
            LOG_ERROR("[Vulkan] Failed to create camera UBO");
            return;
        }

        if (vkMapMemory(
                m_device.GetDevice(),
                m_cameraUBOBufferMemory,
                0,
                cameraBufferSize,
                0,
                &m_cameraMappedData) != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to map camera UBO");
            return;
        }

        m_lightingUboAlignment =
            (sizeof(LightingBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);

        VkDeviceSize lightingBufferSize =
            m_lightingUboAlignment * MAX_FRAMES_IN_FLIGHT;

        if (!m_resourceManager.CreateBuffer(
                lightingBufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_lightingUBOBuffer,
                m_lightingUBOBufferMemory))
        {
            LOG_ERROR("[Vulkan] Failed to create lighting UBO");
            vkUnmapMemory(m_device.GetDevice(), m_cameraUBOBufferMemory);
            m_cameraMappedData = nullptr;
            return;
        }

        if (vkMapMemory(
                m_device.GetDevice(),
                m_lightingUBOBufferMemory,
                0,
                lightingBufferSize,
                0,
                &m_lightingMappedData) != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to map lighting UBO");
            vkUnmapMemory(m_device.GetDevice(), m_cameraUBOBufferMemory);
            m_cameraMappedData = nullptr;
            return;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_resourceManager.GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;

        VkDescriptorSetLayout globalLayout =
            m_resourceManager.GetGlobalSetLayout();

        allocInfo.pSetLayouts = &globalLayout;

        if (vkAllocateDescriptorSets(
                m_device.GetDevice(),
                &allocInfo,
                &m_globalDescriptorSet) != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to allocate global descriptor set");

            vkUnmapMemory(m_device.GetDevice(), m_lightingUBOBufferMemory);
            m_lightingMappedData = nullptr;

            vkUnmapMemory(m_device.GetDevice(), m_cameraUBOBufferMemory);
            m_cameraMappedData = nullptr;

            return;
        }

        VkDescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = m_cameraUBOBuffer;
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraBlock);

        VkDescriptorBufferInfo lightingBufferInfo{};
        lightingBufferInfo.buffer = m_lightingUBOBuffer;
        lightingBufferInfo.offset = 0;
        lightingBufferInfo.range = sizeof(LightingBlock);

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_globalDescriptorSet;
        writes[0].dstBinding = kCameraBlockBinding;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraBufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_globalDescriptorSet;
        writes[1].dstBinding = kLightingBlockBinding;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightingBufferInfo;

        vkUpdateDescriptorSets(
            m_device.GetDevice(),
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr);
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

    void VulkanRenderer::TransitionImage(VkCommandBuffer cmd, VulkanTexture& texture, VkImageAspectFlagBits flagBits, VkImageLayout newLayout)
    {
        if (texture.Layout == newLayout) return;

        const bool isDepth = (flagBits & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;

        VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2        srcAccess = VK_ACCESS_2_NONE;
        VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2        dstAccess = VK_ACCESS_2_NONE;

        switch (texture.Layout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // must chain with the acquire semaphore (waits at COLOR_ATTACHMENT_OUTPUT)
            srcStage = isDepth ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            srcStage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            srcStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; // execution dependency (WAR)
            break;
        default:
            srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            srcAccess = VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        }

        switch (newLayout)
        {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dstStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            break; // NONE/NONE, the submit's signal semaphore covers it
        default:
            dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        }

        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = srcStage;   barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;   barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = texture.Layout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.Image;
        barrier.subresourceRange = { static_cast<VkImageAspectFlags>(flagBits), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };

        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dep);
        texture.Layout = newLayout;
    }

    void VulkanRenderer::ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const RenderPassInfo& info, uint32_t passIndex)
    {
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(info.Target);

        if (m_cameraMappedData != nullptr)
        {
            CameraBlock block{};
            block.ViewProj = info.ViewProj;
            block.Position = glm::vec4(info.CameraPosition, 0.0f);

            uint32_t globalPassIndex = (m_currentFrameIndex * 16) + passIndex;
            VkDeviceSize offset = globalPassIndex * m_cameraUboAlignment;
            std::memcpy(static_cast<char*>(m_cameraMappedData) + offset, &block, sizeof(CameraBlock));
        }

        BeginRenderingOnTarget(cmdBuffer, rt, info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);
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
            VulkanTexture& depth = m_resourceManager.GetTexture(rt.DepthAttachment);
            TransitionImage(cmdBuffer, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void VulkanRenderer::ExecuteRawPass(VkCommandBuffer cmdBuffer, const RawPass& pass)
    {
        const RawPassInfo& info = pass.Info();
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(info.Target);

        BeginRenderingOnTarget(cmdBuffer, rt, info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);

        NativeFrameHandles handles{};
        handles.Device = m_device.GetDevice();
        handles.CommandBuffer = cmdBuffer;
        handles.ColorFormat = static_cast<uint32_t>(m_resourceManager.GetTexture(rt.ColorAttachments[0]).Format);
        info.Callback(handles);

        ExecuteEndRenderPass(cmdBuffer, rt);
    }

    void VulkanRenderer::BeginRenderingOnTarget(VkCommandBuffer cmdBuffer, VulkanRenderTarget &rt, ClearFlags flags,
        const glm::vec4 &clearColorValue, float clearDepthValue, uint32_t clearStencilValue)
    {
        const bool clearColor = HasClearFlag(flags, ClearFlags::Color);
        const bool clearDepth = HasClearFlag(flags, ClearFlags::Depth);

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
            attachment.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.clearValue.color = { clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a };
            colorAttachments.push_back(attachment);
        }

        VkRenderingAttachmentInfo depthAttachment{};
        const bool hasDepth = static_cast<bool>(rt.DepthAttachment);

        if (hasDepth)
        {
            VulkanTexture& depth = m_resourceManager.GetTexture(rt.DepthAttachment);
            TransitionImage(cmdBuffer, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depth.View;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = { clearDepthValue, clearStencilValue };
        }

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea = { {0, 0}, {rt.Width, rt.Height} };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        rendering.pColorAttachments = colorAttachments.data();
        if (hasDepth) rendering.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmdBuffer, &rendering);

        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(rt.Width), static_cast<float>(rt.Height), 0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, {rt.Width, rt.Height} };
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
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
        uint32_t dynamicOffsets[2] = {
            static_cast<uint32_t>(globalPassIndex * m_cameraUboAlignment),
            static_cast<uint32_t>(m_currentFrameIndex * m_lightingUboAlignment)
        };

        VkDescriptorSet descriptorSets[] = { m_globalDescriptorSet, material.DescriptorSet };
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, 0, 2, descriptorSets, 2, dynamicOffsets);

        uint32_t indexCount = static_cast<uint32_t>(indexBuffer.Size / Index::Size(indexBuffer.Type));
        vkCmdDrawIndexed(cmdBuffer, indexCount, 1, 0, 0, 0);
    }

    FrameContext VulkanRenderer::BeginFrame(const FrameLightingData& lighting)
    {
        m_frames[m_currentFrameIndex].Reset();

        VulkanSurface& surface = m_resourceManager.GetRenderSurface();
        VkFence frameFence = m_frameSyncs[m_currentFrameIndex].InFlightFence;

        vkWaitForFences(m_device.GetDevice(), 1, &frameFence, VK_TRUE, UINT64_MAX);

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
            m_resourceManager.RecreateSwapchain(surface);
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

        m_frames[m_currentFrameIndex].Init(m_resourceManager.GetCurrentBackBuffer(), true);

        return m_frames[m_currentFrameIndex].CreateContext();
    }

    void VulkanRenderer::EndFrame(const FrameContext& frameContext)
    {
        Frame& frame = m_frames[frameContext.GetIndex()];
        VkCommandBuffer cmdBuffer = m_frameSyncs[frameContext.GetIndex()].CommandBuffer;

        uint32_t passIndex = 0;
        for (const auto& entry : frame.GetPasses())
        {
            if (const auto* materialPass = std::get_if<RenderPass>(&entry))
            {
                const RenderPassInfo& passInfo = materialPass->Info();
                VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(passInfo.Target);

                ExecuteBeginRenderPass(cmdBuffer, passInfo, passIndex);
                for (const auto& draw : materialPass->GetDraws())
                    ExecuteDraw(cmdBuffer, draw, passInfo.Target, passIndex);
                ExecuteEndRenderPass(cmdBuffer, rt);
                ++passIndex;
            }
            else if (const auto* rawPass = std::get_if<RawPass>(&entry))
            {
                ExecuteRawPass(cmdBuffer, *rawPass);
            }
        }

        if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS)
            LOG_ERROR("[Renderer] Failed to end command buffer");

        VulkanSurface& surface = m_resourceManager.GetRenderSurface();
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
            m_resourceManager.RecreateSwapchain(surface);

        m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}