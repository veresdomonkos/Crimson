#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "crimson/core/profiler.hpp"
#include <cstring>
#include <vector>

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

        TracyVkDestroy(m_tracyContext);
    }

    void VulkanRenderer::InitializeSynchronizationAndCommands()
    {
        const vk::raii::Device& device = m_device.GetDevice();

        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamily();

        m_commandPool = device.createCommandPool(poolInfo);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = *m_commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        std::vector<vk::raii::CommandBuffer> buffers = device.allocateCommandBuffers(allocInfo);

        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_frames[i].SetIndex(i);
            m_frameSyncs[i].CommandBuffer = std::move(buffers[i]);
            m_frameSyncs[i].ImageAvailableSemaphore = device.createSemaphore({});
            m_frameSyncs[i].InFlightFence = device.createFence(fenceInfo);
        }

        m_tracyContext = TracyVkContext(
            static_cast<VkPhysicalDevice>(*m_device.GetPhysicalDevice()),
            static_cast<VkDevice>(*device),
            static_cast<VkQueue>(*m_device.GetGraphicsQueue()),
            static_cast<VkCommandBuffer>(*m_frameSyncs[0].CommandBuffer));
    }

    void VulkanRenderer::InitGlobals()
    {
        vk::DeviceSize minUboAlignment =
            m_device.GetProperties().limits.minUniformBufferOffsetAlignment;

        m_cameraUboAlignment =
            (sizeof(CameraBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);

        vk::DeviceSize cameraBufferSize =
            m_cameraUboAlignment * MaxRenderPasses * MAX_FRAMES_IN_FLIGHT;

        m_cameraUBO = CreateAllocatedBuffer(m_device.GetAllocator(), cameraBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, BufferMemory::HostMapped);

        if (!m_cameraUBO)
        {
            LOG_ERROR("[Vulkan Renderer] Failed to create camera UBO");
            return;
        }

        m_lightingUboAlignment =
            (sizeof(LightingBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);

        vk::DeviceSize lightingBufferSize =
            m_lightingUboAlignment * MAX_FRAMES_IN_FLIGHT;

        m_lightingUBO = CreateAllocatedBuffer(m_device.GetAllocator(), lightingBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, BufferMemory::HostMapped);

        if (!m_lightingUBO)
        {
            LOG_ERROR("[Vulkan Renderer] Failed to create lighting UBO");
            return;
        }

        const vk::DescriptorSetLayout globalLayout = m_resourceManager.GetGlobalSetLayout();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_resourceManager.GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &globalLayout;

        m_globalDescriptorSet = std::move(m_device.GetDevice().allocateDescriptorSets(allocInfo).front());

        vk::DescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = m_cameraUBO.Get();
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraBlock);

        vk::DescriptorBufferInfo lightingBufferInfo{};
        lightingBufferInfo.buffer = m_lightingUBO.Get();
        lightingBufferInfo.offset = 0;
        lightingBufferInfo.range = sizeof(LightingBlock);

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = *m_globalDescriptorSet;
        writes[0].dstBinding = kCameraBlockBinding;
        writes[0].descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraBufferInfo;

        writes[1].dstSet = *m_globalDescriptorSet;
        writes[1].dstBinding = kLightingBlockBinding;
        writes[1].descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightingBufferInfo;

        m_device.GetDevice().updateDescriptorSets(writes, nullptr);
    }

    void VulkanRenderer::SetShadowMap(TextureHandle shadowMap)
    {
        const VulkanTexture& tex = m_resourceManager.GetTexture(shadowMap);

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_resourceManager.GetDefaultSampler();
        imageInfo.imageView = *tex.View;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = *m_globalDescriptorSet;
        write.dstBinding = kShadowMapBinding;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device.GetDevice().updateDescriptorSets(write, nullptr);
    }

    void VulkanRenderer::TransitionImage(const vk::raii::CommandBuffer& cmd, VulkanTexture& texture, vk::ImageLayout newLayout)
    {
        if (texture.Layout == newLayout) return;

        const bool isDepth = static_cast<bool>(texture.Aspect & vk::ImageAspectFlagBits::eDepth);

        vk::PipelineStageFlags2 srcStage = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2        srcAccess = vk::AccessFlagBits2::eNone;
        vk::PipelineStageFlags2 dstStage = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2        dstAccess = vk::AccessFlagBits2::eNone;

        switch (texture.Layout)
        {
        case vk::ImageLayout::eUndefined:
        case vk::ImageLayout::ePresentSrcKHR:
            // must chain with the acquire semaphore (waits at COLOR_ATTACHMENT_OUTPUT)
            srcStage = isDepth ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            break;
        case vk::ImageLayout::eColorAttachmentOptimal:
            srcStage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            srcAccess = vk::AccessFlagBits2::eColorAttachmentWrite;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            srcStage  = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            srcAccess = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            srcStage = vk::PipelineStageFlagBits2::eFragmentShader; // execution dependency (WAR)
            break;
        default:
            srcStage = vk::PipelineStageFlagBits2::eAllCommands;
            srcAccess = vk::AccessFlagBits2::eMemoryWrite;
            break;
        }

        switch (newLayout)
        {
        case vk::ImageLayout::eColorAttachmentOptimal:
            dstStage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            dstAccess = vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            dstStage  = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            dstAccess = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            dstStage  = vk::PipelineStageFlagBits2::eFragmentShader;
            dstAccess = vk::AccessFlagBits2::eShaderSampledRead;
            break;
        case vk::ImageLayout::ePresentSrcKHR:
            break; // NONE/NONE, the submit's signal semaphore covers it
        default:
            dstStage = vk::PipelineStageFlagBits2::eAllCommands;
            dstAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            break;
        }

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = srcStage;   barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;   barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = texture.Layout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.Image;
        barrier.subresourceRange = vk::ImageSubresourceRange{ texture.Aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };

        vk::DependencyInfo dep{};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;

        cmd.pipelineBarrier2(dep);
        texture.Layout = newLayout;
    }

    void VulkanRenderer::ExecuteBeginRenderPass(const vk::raii::CommandBuffer& cmdBuffer, const RenderPassInfo& info, uint32_t passIndex)
    {
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(info.Target);

        if (m_cameraUBO)
        {
            CameraBlock block{};
            block.ViewProj = info.ViewProj;
            block.Position = glm::vec4(info.CameraPosition, 0.0f);

            uint32_t globalPassIndex = (m_currentFrameIndex * MaxRenderPasses) + passIndex;
            vk::DeviceSize offset = globalPassIndex * m_cameraUboAlignment;
            std::memcpy(static_cast<char*>(m_cameraUBO.Mapped()) + offset, &block, sizeof(CameraBlock));
        }

        BeginRenderingOnTarget(cmdBuffer, rt, info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);

        // The camera's dynamic offset is different for every pass
        m_bound.GlobalsBound = false;
    }

    void VulkanRenderer::ExecuteEndRenderPass(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt)
    {
        cmdBuffer.endRendering();

        for (TextureHandle colorHandle : rt.ColorAttachments)
        {
            VulkanTexture& color = m_resourceManager.GetTexture(colorHandle);
            const vk::ImageLayout targetLayout = rt.IsSwapchain ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eShaderReadOnlyOptimal;
            TransitionImage(cmdBuffer, color, targetLayout);
        }

        if (rt.DepthAttachment && !rt.IsSwapchain)
        {
            VulkanTexture& depth = m_resourceManager.GetTexture(rt.DepthAttachment);
            TransitionImage(cmdBuffer, depth, vk::ImageLayout::eShaderReadOnlyOptimal);
        }
    }

    void VulkanRenderer::ExecuteRawPass(const vk::raii::CommandBuffer& cmdBuffer, const RawPass& pass)
    {
        const RawPassInfo& info = pass.Info();
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(info.Target);

        BeginRenderingOnTarget(cmdBuffer, rt, info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);

        NativeFrameHandles handles{};
        handles.Device = static_cast<VkDevice>(*m_device.GetDevice());
        handles.CommandBuffer = static_cast<VkCommandBuffer>(*cmdBuffer);
        handles.ColorFormat = static_cast<uint32_t>(m_resourceManager.GetTexture(rt.ColorAttachments[0]).Format);
        info.Callback(handles);

        // The callback records its own pipeline, buffer and descriptor set binds
        m_bound = {};

        ExecuteEndRenderPass(cmdBuffer, rt);
    }

    void VulkanRenderer::BeginRenderingOnTarget(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget &rt, ClearFlags flags,
        const glm::vec4 &clearColorValue, float clearDepthValue, uint32_t clearStencilValue)
    {
        const bool clearColor = HasClearFlag(flags, ClearFlags::Color);
        const bool clearDepth = HasClearFlag(flags, ClearFlags::Depth);
        const bool clearStencil = HasClearFlag(flags, ClearFlags::Stencil);

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(rt.ColorAttachments.size());

        for (TextureHandle colorHandle : rt.ColorAttachments)
        {
            VulkanTexture& color = m_resourceManager.GetTexture(colorHandle);
            TransitionImage(cmdBuffer, color, vk::ImageLayout::eColorAttachmentOptimal);

            vk::RenderingAttachmentInfo attachment{};
            attachment.imageView = *color.View;
            attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            attachment.loadOp = clearColor ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            attachment.storeOp = vk::AttachmentStoreOp::eStore;
            attachment.clearValue.color = vk::ClearColorValue(clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a);
            colorAttachments.push_back(attachment);
        }

        vk::RenderingAttachmentInfo depthAttachment{};
        vk::RenderingAttachmentInfo stencilAttachment{};
        const bool hasDepth = static_cast<bool>(rt.DepthAttachment);
        bool hasStencil = false;

        if (hasDepth)
        {
            VulkanTexture& depth = m_resourceManager.GetTexture(rt.DepthAttachment);
            TransitionImage(cmdBuffer, depth, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            hasStencil = static_cast<bool>(depth.Aspect & vk::ImageAspectFlagBits::eStencil);

            depthAttachment.imageView = hasStencil ? *depth.AttachmentView : *depth.View;
            depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthAttachment.loadOp = clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(clearDepthValue, clearStencilValue);

            stencilAttachment = depthAttachment;
            stencilAttachment.loadOp = clearStencil ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
        }

        vk::RenderingInfo rendering{};
        rendering.renderArea = vk::Rect2D{ vk::Offset2D{ 0, 0 }, vk::Extent2D{ rt.Width, rt.Height } };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        rendering.pColorAttachments = colorAttachments.data();
        if (hasDepth) rendering.pDepthAttachment = &depthAttachment;
        if (hasStencil) rendering.pStencilAttachment = &stencilAttachment;

        cmdBuffer.beginRendering(rendering);

        cmdBuffer.setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(rt.Width), static_cast<float>(rt.Height), 0.0f, 1.0f });
        cmdBuffer.setScissor(0, rendering.renderArea);
    }

    void VulkanRenderer::ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawInfo& draw, RenderTargetHandle target, uint32_t passIndex)
    {
        CRIMSON_PROFILE_FUNCTION();

        VulkanVertexBuffer& vertexBuffer = m_resourceManager.GetVertexBuffer(draw.VertexBuffer);
        VulkanIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(draw.IndexBuffer);
        VulkanMaterial& material = m_resourceManager.GetMaterial(draw.Material);

        if (!*material.DescriptorSet) return;

        if (!m_bound.PipelineEntry || m_bound.Shader != material.Shader || m_bound.Settings != material.Settings
            || m_bound.Target != target || m_bound.VertexBufferHandle != draw.VertexBuffer)
        {
            m_bound.PipelineEntry = &m_resourceManager.GetOrCreateGraphicsPipeline({
                .Layout = vertexBuffer.Layout, .Shader = material.Shader, .Target = target, .Settings = material.Settings
            });
            m_bound.Shader = material.Shader;
            m_bound.Settings = material.Settings;
            m_bound.Target = target;
            m_bound.VertexBufferHandle = draw.VertexBuffer;
        }

        const VulkanGraphicsPipeline& pipeline = *m_bound.PipelineEntry;
        if (!*pipeline.Pipeline) return;

        if (m_bound.Pipeline != *pipeline.Pipeline)
        {
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.Pipeline);
            m_bound.Pipeline = *pipeline.Pipeline;
            ++m_stats.PipelineBinds;
        }

        // Each shader has its own layout. Set 0 is the same in all of them and stays valid,
        // but set 1 differs, so a new layout invalidates the bound material.
        if (m_bound.Layout != pipeline.Layout)
        {
            m_bound.Layout = pipeline.Layout;
            m_bound.MaterialSet = nullptr;
        }

        const vk::Buffer vertexBufferHandle = vertexBuffer.Buffer.Get();
        const vk::Buffer indexBufferHandle = indexBuffer.Buffer.Get();

        if (m_bound.VertexBuffer != vertexBufferHandle || m_bound.IndexBuffer != indexBufferHandle)
            ++m_stats.BufferBinds;

        if (m_bound.VertexBuffer != vertexBufferHandle)
        {
            cmdBuffer.bindVertexBuffers(0, vertexBufferHandle, vk::DeviceSize{ 0 });
            m_bound.VertexBuffer = vertexBufferHandle;
        }

        if (m_bound.IndexBuffer != indexBufferHandle)
        {
            cmdBuffer.bindIndexBuffer(indexBufferHandle, 0, indexBuffer.Type == IndexType::UInt32 ? vk::IndexType::eUint32 : vk::IndexType::eUint16);
            m_bound.IndexBuffer = indexBufferHandle;
        }

        if (!m_bound.GlobalsBound)
        {
            uint32_t globalPassIndex = (m_currentFrameIndex * MaxRenderPasses) + passIndex;
            const std::array<uint32_t, 2> dynamicOffsets = {
                static_cast<uint32_t>(globalPassIndex * m_cameraUboAlignment),
                static_cast<uint32_t>(m_currentFrameIndex * m_lightingUboAlignment)
            };

            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, 0, *m_globalDescriptorSet, dynamicOffsets);
            m_bound.GlobalsBound = true;
        }

        if (m_bound.MaterialSet != *material.DescriptorSet)
        {
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, 1, *material.DescriptorSet, nullptr);
            m_bound.MaterialSet = *material.DescriptorSet;
            ++m_stats.MaterialBinds;
        }

        const uint32_t total = static_cast<uint32_t>(indexBuffer.Size / Index::Size(indexBuffer.Type));
        const uint32_t indexCount = draw.IndexCount ? draw.IndexCount : total;
        cmdBuffer.drawIndexed(indexCount, 1, draw.FirstIndex, 0, 0);

        ++m_stats.DrawCalls;
        m_stats.Triangles += indexCount / 3;
    }

    FrameContext VulkanRenderer::BeginFrame(const FrameLightingData& lighting)
    {
        CRIMSON_PROFILE_FUNCTION();

        m_stats = {};
        m_frames[m_currentFrameIndex].Reset();

        uint32_t windowWidth = m_device.GetPrimaryWindow().Width();
        uint32_t windowHeight = m_device.GetPrimaryWindow().Height();

        if (windowWidth == 0 || windowHeight == 0)
            return m_frames[m_currentFrameIndex].CreateContext();

        VulkanSurface& surface = m_resourceManager.GetRenderSurface();

        if (surface.IsDirty)
        {
            if (!m_resourceManager.RecreateSwapchain(surface))
                return m_frames[m_currentFrameIndex].CreateContext();
        }

        const vk::raii::Device& device = m_device.GetDevice();
        FrameSync& sync = m_frameSyncs[m_currentFrameIndex];

        {
            CRIMSON_PROFILE_SCOPE("Wait For Frame Fence");
            ScopedWaitTimer waitTimer(m_stats);
            (void)device.waitForFences(*sync.InFlightFence, VK_TRUE, UINT64_MAX);
        }

        if (m_lightingUBO)
        {
            LightingBlock block{};
            block.AmbientColor = glm::vec4(lighting.AmbientColor, 0.0f);
            block.ShadowViewProj = lighting.ShadowViewProj;
            block.ShadowLightIndex = lighting.ShadowLightIndex;
            block.LightCount = std::min<uint32_t>(static_cast<uint32_t>(lighting.Lights.size()), kMaxLights);

            for (uint32_t i = 0; i < block.LightCount; ++i)
                block.Lights[i] = lighting.Lights[i].ToGPULight();

            vk::DeviceSize offset = m_currentFrameIndex * m_lightingUboAlignment;
            std::memcpy(static_cast<char*>(m_lightingUBO.Mapped()) + offset, &block, sizeof(LightingBlock));
        }

        uint32_t imageIndex = 0;
        vk::Result result = vk::Result::eSuccess;

        try
        {
            CRIMSON_PROFILE_SCOPE("Acquire Swapchain Image");
            ScopedWaitTimer waitTimer(m_stats);
            const auto acquired = surface.Swapchain.acquireNextImage(UINT64_MAX, *sync.ImageAvailableSemaphore);
            result = acquired.result;
            imageIndex = acquired.value;
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Vulkan Renderer] AcquireNextImage failed: {}", error.what());
            return m_frames[m_currentFrameIndex].CreateContext();
        }

        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            surface.IsDirty = true;
            return m_frames[m_currentFrameIndex].CreateContext();
        }

        surface.CurrentImageIndex = imageIndex;
        device.resetFences(*sync.InFlightFence);

        sync.CommandBuffer.reset();

        vk::CommandBufferBeginInfo begin{};
        begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        sync.CommandBuffer.begin(begin);
        m_bound = {};

        TracyVkCollect(m_tracyContext, static_cast<VkCommandBuffer>(*sync.CommandBuffer));

        m_frames[m_currentFrameIndex].Init(m_resourceManager.GetCurrentBackBuffer(), true);

        return m_frames[m_currentFrameIndex].CreateContext();
    }

    void VulkanRenderer::EndFrame(const FrameContext& frameContext)
    {
        CRIMSON_PROFILE_FUNCTION();

        Frame& frame = m_frames[frameContext.GetIndex()];
        FrameSync& sync = m_frameSyncs[frameContext.GetIndex()];
        const vk::raii::CommandBuffer& cmdBuffer = sync.CommandBuffer;
        [[maybe_unused]] const VkCommandBuffer tracyCmdBuffer = static_cast<VkCommandBuffer>(*cmdBuffer);

        uint32_t passIndex = 0;
        m_stats.Passes = static_cast<uint32_t>(frame.GetPasses().size());

        for (const auto& entry : frame.GetPasses())
        {
            if (const auto* materialPass = std::get_if<RenderPass>(&entry))
            {
                CRIMSON_PROFILE_SCOPE("Render Pass");
                TracyVkZone(m_tracyContext, tracyCmdBuffer, "Render Pass");

                const RenderPassInfo& passInfo = materialPass->Info();
                VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(passInfo.Target);

                ExecuteBeginRenderPass(cmdBuffer, passInfo, passIndex);

                m_resourceManager.BuildDrawOrder(materialPass->GetDraws(), m_drawOrder);
                for (const SortedDraw& sorted : m_drawOrder)
                    ExecuteDraw(cmdBuffer, *sorted.Draw, passInfo.Target, passIndex);

                ExecuteEndRenderPass(cmdBuffer, rt);
                ++passIndex;
            }
            else if (const auto* rawPass = std::get_if<RawPass>(&entry))
            {
                CRIMSON_PROFILE_SCOPE("Raw Pass");
                TracyVkZone(m_tracyContext, tracyCmdBuffer, "Raw Pass");

                ExecuteRawPass(cmdBuffer, *rawPass);
            }
        }

        CRIMSON_PROFILE_PLOT("Draw Calls", m_stats.DrawCalls);

        cmdBuffer.end();

        VulkanSurface& surface = m_resourceManager.GetRenderSurface();
        uint32_t imageIndex = surface.CurrentImageIndex;

        const vk::Semaphore waitSemaphore = *sync.ImageAvailableSemaphore;
        const vk::Semaphore signalSemaphore = *surface.RenderFinishedSemaphores[imageIndex];
        const vk::CommandBuffer rawCmdBuffer = *cmdBuffer;
        const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

        vk::SubmitInfo submit{};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &waitSemaphore;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &rawCmdBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signalSemaphore;

        const vk::raii::Queue& queue = m_device.GetGraphicsQueue();

        {
            CRIMSON_PROFILE_SCOPE("Queue Submit");
            queue.submit(submit, *sync.InFlightFence);
        }

        const vk::SwapchainKHR swapchain = *surface.Swapchain;

        vk::PresentInfoKHR present{};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &signalSemaphore;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;

        vk::Result result;
        {
            CRIMSON_PROFILE_SCOPE("Queue Present");
            ScopedWaitTimer waitTimer(m_stats);
            result = queue.presentKHR(present);
        }

        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
        {
            surface.IsDirty = true;
        }

        m_lastStats = m_stats;
        m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}
