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
        const vk::raii::Device& device = m_device.GetDevice();
        const VmaAllocator allocator = m_device.GetAllocator();

        const vk::DeviceSize minUboAlignment = m_device.GetProperties().limits.minUniformBufferOffsetAlignment;
        m_viewUboAlignment = (sizeof(ViewBlock) + minUboAlignment - 1) & ~(minUboAlignment - 1);

        m_viewUBO = CreateAllocatedBuffer(allocator, m_viewUboAlignment * MaxRenderPasses * MAX_FRAMES_IN_FLIGHT, vk::BufferUsageFlagBits::eUniformBuffer, BufferMemory::HostMapped);

        if (!m_viewUBO)
        {
            LOG_ERROR("[Vulkan Renderer] Failed to create the view UBO");
            return;
        }

        const vk::DescriptorSetLayout viewLayout = m_resourceManager.GetViewSetLayout();

        vk::DescriptorSetAllocateInfo viewAllocInfo{};
        viewAllocInfo.descriptorPool = m_resourceManager.GetDescriptorPool();
        viewAllocInfo.descriptorSetCount = 1;
        viewAllocInfo.pSetLayouts = &viewLayout;

        m_viewDescriptorSet = std::move(device.allocateDescriptorSets(viewAllocInfo).front());

        vk::DescriptorBufferInfo viewBufferInfo{};
        viewBufferInfo.buffer = m_viewUBO.Get();
        viewBufferInfo.offset = 0;
        viewBufferInfo.range = sizeof(ViewBlock);

        vk::WriteDescriptorSet viewWrite{};
        viewWrite.dstSet = *m_viewDescriptorSet;
        viewWrite.dstBinding = kViewBlockBinding;
        viewWrite.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        viewWrite.descriptorCount = 1;
        viewWrite.pBufferInfo = &viewBufferInfo;

        device.updateDescriptorSets(viewWrite, nullptr);

        const vk::DescriptorSetLayout frameLayout = m_resourceManager.GetFrameSetLayout();

        for (FrameResources& resources : m_frameResources)
        {
            vk::DescriptorSetAllocateInfo frameAllocInfo{};
            frameAllocInfo.descriptorPool = m_resourceManager.GetDescriptorPool();
            frameAllocInfo.descriptorSetCount = 1;
            frameAllocInfo.pSetLayouts = &frameLayout;

            resources.DescriptorSet = std::move(device.allocateDescriptorSets(frameAllocInfo).front());

            resources.FrameBlockBuffer = CreateAllocatedBuffer(allocator, sizeof(FrameBlock), vk::BufferUsageFlagBits::eUniformBuffer, BufferMemory::HostMapped);

            if (!resources.FrameBlockBuffer)
            {
                LOG_ERROR("[Vulkan Renderer] Failed to create the frame UBO");
                return;
            }

            vk::DescriptorBufferInfo frameBufferInfo{};
            frameBufferInfo.buffer = resources.FrameBlockBuffer.Get();
            frameBufferInfo.offset = 0;
            frameBufferInfo.range = sizeof(FrameBlock);

            vk::WriteDescriptorSet frameWrite{};
            frameWrite.dstSet = *resources.DescriptorSet;
            frameWrite.dstBinding = kFrameBlockBinding;
            frameWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
            frameWrite.descriptorCount = 1;
            frameWrite.pBufferInfo = &frameBufferInfo;

            device.updateDescriptorSets(frameWrite, nullptr);

            // Every binding has to point at something before the first frame, whatever it ends up holding
            constexpr std::size_t kInitialStorageSize = 16 * 1024;
            UploadStorageBuffer(resources, resources.Lights, kLightsBinding, nullptr, kInitialStorageSize);
            UploadStorageBuffer(resources, resources.Shadows, kShadowsBinding, nullptr, kInitialStorageSize);
            UploadStorageBuffer(resources, resources.Objects, kObjectsBinding, nullptr, kInitialStorageSize);
        }
    }

    // Only call this for a frame whose fence has been waited on, as it can free the buffer the frame used last
    void VulkanRenderer::UploadStorageBuffer(const FrameResources& resources, FrameStorageBuffer& storage, uint32_t binding, const void* data, std::size_t size)
    {
        if (size > storage.Capacity)
        {
            vk::DeviceSize capacity = std::max<vk::DeviceSize>(storage.Capacity, 1);
            while (capacity < size)
                capacity *= 2;

            AllocatedBuffer buffer = CreateAllocatedBuffer(m_device.GetAllocator(), capacity, vk::BufferUsageFlagBits::eStorageBuffer, BufferMemory::HostMapped);

            if (!buffer)
            {
                LOG_ERROR("[Vulkan Renderer] Failed to grow a storage buffer to {} bytes", capacity);
                return;
            }

            storage.Buffer = std::move(buffer);
            storage.Capacity = capacity;

            vk::DescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = storage.Buffer.Get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write{};
            write.dstSet = *resources.DescriptorSet;
            write.dstBinding = binding;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;

            m_device.GetDevice().updateDescriptorSets(write, nullptr);
        }

        if (data)
            std::memcpy(storage.Buffer.Mapped(), data, size);
    }

    void VulkanRenderer::SetShadowMap(TextureHandle shadowMap)
    {
        VulkanTexture& tex = m_resourceManager.GetTexture(shadowMap);

        // Called between frames, so frames still in flight must finish before their sets change
        m_device.WaitIdle();

        // Every frame binds it, including those without a shadow pass to have rendered to it first
        if (tex.Layout == vk::ImageLayout::eUndefined)
        {
            m_device.ImmediateSubmit([&](vk::CommandBuffer commandBuffer)
            {
                vk::ImageMemoryBarrier2 barrier{};
                barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
                barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
                barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
                barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = tex.Image;
                barrier.subresourceRange = vk::ImageSubresourceRange{ tex.Aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };

                vk::DependencyInfo dependency{};
                dependency.imageMemoryBarrierCount = 1;
                dependency.pImageMemoryBarriers = &barrier;
                commandBuffer.pipelineBarrier2(dependency);
            });

            tex.Layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageView = *tex.View;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        for (const FrameResources& resources : m_frameResources)
        {
            vk::WriteDescriptorSet write{};
            write.dstSet = *resources.DescriptorSet;
            write.dstBinding = kShadowAtlasBinding;
            write.descriptorType = vk::DescriptorType::eSampledImage;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            m_device.GetDevice().updateDescriptorSets(write, nullptr);
        }
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

        if (m_viewUBO)
        {
            ViewBlock block{};
            block.ViewProj = info.ViewProj;
            block.CameraPosition = glm::vec4(info.CameraPosition, 0.0f);

            uint32_t globalPassIndex = (m_currentFrameIndex * MaxRenderPasses) + passIndex;
            vk::DeviceSize offset = globalPassIndex * m_viewUboAlignment;
            std::memcpy(static_cast<char*>(m_viewUBO.Mapped()) + offset, &block, sizeof(ViewBlock));
        }

        BeginRenderingOnTarget(cmdBuffer, rt, info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil, info.Viewport);

        // The view block's dynamic offset is different for every pass
        m_bound.ViewSetBound = false;
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

        BeginRenderingOnTarget(cmdBuffer, rt, info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil, std::nullopt);

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
        const glm::vec4 &clearColorValue, float clearDepthValue, uint32_t clearStencilValue, const std::optional<RenderRect>& area)
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
        // A load op clear only touches the render area, so a pass can clear just its part of the target
        const RenderRect rect = area.value_or(RenderRect{ 0, 0, rt.Width, rt.Height });
        rendering.renderArea = vk::Rect2D{ vk::Offset2D{ rect.X, rect.Y }, vk::Extent2D{ rect.Width, rect.Height } };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        rendering.pColorAttachments = colorAttachments.data();
        if (hasDepth) rendering.pDepthAttachment = &depthAttachment;
        if (hasStencil) rendering.pStencilAttachment = &stencilAttachment;

        cmdBuffer.beginRendering(rendering);

        cmdBuffer.setViewport(0, vk::Viewport{ static_cast<float>(rect.X), static_cast<float>(rect.Y), static_cast<float>(rect.Width), static_cast<float>(rect.Height), 0.0f, 1.0f });
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

        // Each shader has its own layout. The frame and view sets and the push constant are the same
        // in all of them and stay valid, but the material set differs, so a new layout invalidates it.
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

        if (!m_bound.FrameSetBound)
        {
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, kFrameSpace, *m_frameResources[m_currentFrameIndex].DescriptorSet, nullptr);
            m_bound.FrameSetBound = true;
        }

        if (!m_bound.ViewSetBound)
        {
            const uint32_t globalPassIndex = (m_currentFrameIndex * MaxRenderPasses) + passIndex;
            const uint32_t dynamicOffset = static_cast<uint32_t>(globalPassIndex * m_viewUboAlignment);

            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, kViewSpace, *m_viewDescriptorSet, dynamicOffset);
            m_bound.ViewSetBound = true;
        }

        if (m_bound.MaterialSet != *material.DescriptorSet)
        {
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, kMaterialSpace, *material.DescriptorSet, nullptr);
            m_bound.MaterialSet = *material.DescriptorSet;
            ++m_stats.MaterialBinds;
        }

        if (m_bound.Object != draw.Object)
        {
            const DrawData drawData{ draw.Object };
            cmdBuffer.pushConstants<DrawData>(pipeline.Layout, vk::ShaderStageFlagBits::eVertex, 0, drawData);
            m_bound.Object = draw.Object;
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

        m_gpuData.Build(lighting);

        FrameResources& resources = m_frameResources[m_currentFrameIndex];

        std::memcpy(resources.FrameBlockBuffer.Mapped(), &m_gpuData.Block, sizeof(FrameBlock));
        UploadStorageBuffer(resources, resources.Lights, kLightsBinding, m_gpuData.Lights.data(), std::span(m_gpuData.Lights).size_bytes());
        UploadStorageBuffer(resources, resources.Shadows, kShadowsBinding, m_gpuData.Shadows.data(), std::span(m_gpuData.Shadows).size_bytes());

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

        // Nothing has bound this frame's set yet, so the buffer can still grow
        FrameResources& resources = m_frameResources[frameContext.GetIndex()];
        const std::span<const ObjectData> objects = frame.GetObjects();
        UploadStorageBuffer(resources, resources.Objects, kObjectsBinding, objects.data(), objects.size_bytes());

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

                const std::span<const DrawInfo> draws = materialPass->GetDraws();

                BuildDrawOrder(draws, m_resourceManager, m_drawOrder);
                for (const DrawSortEntry& sorted : m_drawOrder)
                    ExecuteDraw(cmdBuffer, draws[sorted.Index], passInfo.Target, passIndex);

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
