#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "crimson/core/profiler.hpp"
#include <cstring>
#include <vector>

namespace crimson::vulkan
{
    namespace
    {
        void SetViewportAndScissor(const vk::raii::CommandBuffer& cmdBuffer, const RenderRect& rect)
        {
            cmdBuffer.setViewport(0, vk::Viewport{ static_cast<float>(rect.X), static_cast<float>(rect.Y), static_cast<float>(rect.Width), static_cast<float>(rect.Height), 0.0f, 1.0f });
            cmdBuffer.setScissor(0, vk::Rect2D{ vk::Offset2D{ rect.X, rect.Y }, vk::Extent2D{ rect.Width, rect.Height } });
        }
    }

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
        allocInfo.commandBufferCount = static_cast<uint32_t>(kMaxFramesInFlight);

        std::vector<vk::raii::CommandBuffer> buffers = device.allocateCommandBuffers(allocInfo);

        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

        for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
        {
            m_inFlight[i].CommandBuffer = std::move(buffers[i]);
            m_inFlight[i].ImageAvailableSemaphore = device.createSemaphore({});
            m_inFlight[i].InFlightFence = device.createFence(fenceInfo);
        }

        m_tracyContext = TracyVkContext(
            static_cast<VkPhysicalDevice>(*m_device.GetPhysicalDevice()),
            static_cast<VkDevice>(*device),
            static_cast<VkQueue>(*m_device.GetGraphicsQueue()),
            static_cast<VkCommandBuffer>(*m_inFlight[0].CommandBuffer));
    }

    void VulkanRenderer::InitGlobals()
    {
        const vk::raii::Device& device = m_device.GetDevice();
        const VmaAllocator allocator = m_device.GetAllocator();

        const vk::DescriptorSetLayout frameLayout = m_resourceManager.GetFrameSetLayout();

        for (InFlightFrame& resources : m_inFlight)
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
            UploadStorageBuffer(resources, resources.Views, kViewsBinding, nullptr, kInitialStorageSize);
        }
    }

    // Only call this for a frame whose fence has been waited on, as it can free the buffer the frame used last
    void VulkanRenderer::UploadStorageBuffer(const InFlightFrame& resources, FrameStorageBuffer& storage, uint32_t binding, const void* data, std::size_t size)
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

    void VulkanRenderer::SetShadowAtlas(TextureHandle shadowAtlas)
    {
        VulkanTexture& tex = m_resourceManager.GetTexture(shadowAtlas);

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

        for (const InFlightFrame& resources : m_inFlight)
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

    void VulkanRenderer::ExecutePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, std::span<const FrameView> views)
    {
        const PassInfo& info = pass.GetInfo();

        CRIMSON_PROFILE_SCOPE_DYNAMIC(info.Name);
        TracyVkZoneTransient(m_tracyContext, passZone, static_cast<VkCommandBuffer>(*cmdBuffer), info.Name, true);

        const RenderTargetHandle target = info.Target ? info.Target : m_resourceManager.GetCurrentBackBuffer();
        VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(target);

        // What the pass samples has to be readable before it starts. Every pass also leaves its attachments readable
        // when it ends, so for now this only changes something for a texture nothing has drawn to yet.
        for (const TextureHandle read : info.Reads)
        {
            if (read)
                TransitionImage(cmdBuffer, m_resourceManager.GetTexture(read), vk::ImageLayout::eShaderReadOnlyOptimal);
        }

        if (pass.IsNative())
        {
            ExecuteNativePass(cmdBuffer, pass, rt);
            return;
        }

        BeginRendering(cmdBuffer, rt, info.Clear, info.Viewport);

        // BeginRendering set the viewport to the pass's area, which the first draw's view may not have
        m_bound.DrawPushed = false;

        const RenderRect passArea = info.Viewport.value_or(RenderRect{ 0, 0, rt.Width, rt.Height });
        const std::span<const DrawItem> draws = pass.GetDraws();

        BuildDrawOrder(draws, m_resourceManager, m_drawOrder);
        for (const DrawSortEntry& sorted : m_drawOrder)
            ExecuteDraw(cmdBuffer, draws[sorted.Index], target, views, passArea);

        EndPass(cmdBuffer, rt);
    }

    void VulkanRenderer::EndPass(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt)
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

    void VulkanRenderer::ExecuteNativePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, VulkanRenderTarget& rt)
    {
        BeginRendering(cmdBuffer, rt, pass.GetInfo().Clear, pass.GetInfo().Viewport);

        NativeContext context{};
        context.Device = static_cast<VkDevice>(*m_device.GetDevice());
        context.CommandBuffer = static_cast<VkCommandBuffer>(*cmdBuffer);
        context.ColorFormat = static_cast<uint32_t>(m_resourceManager.GetTexture(rt.ColorAttachments[0]).Format);
        pass.GetNativeFn()(context);

        // The callback records its own pipeline, buffer and descriptor set binds
        m_bound = {};

        EndPass(cmdBuffer, rt);
    }

    void VulkanRenderer::BeginRendering(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt, const ClearInfo& clear, const std::optional<RenderRect>& area)
    {
        const bool clearColor = HasClearFlag(clear.Flags, ClearFlags::Color);
        const bool clearDepth = HasClearFlag(clear.Flags, ClearFlags::Depth);
        const bool clearStencil = HasClearFlag(clear.Flags, ClearFlags::Stencil);

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
            attachment.clearValue.color = vk::ClearColorValue(clear.Color.r, clear.Color.g, clear.Color.b, clear.Color.a);
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
            depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(clear.Depth, clear.Stencil);

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

        SetViewportAndScissor(cmdBuffer, rect);
    }

    void VulkanRenderer::ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawItem& draw, RenderTargetHandle target, std::span<const FrameView> views, const RenderRect& passArea)
    {
        CRIMSON_PROFILE_FUNCTION();

        // A view whose part of the target is outside the pass's draws nothing
        const RenderRect viewArea = ViewArea(views[draw.View], passArea);
        if (IsEmpty(viewArea)) return;

        VulkanVertexBuffer& vertexBuffer = m_resourceManager.GetVertexBuffer(draw.VertexBuffer);
        VulkanIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(draw.IndexBuffer);
        VulkanMaterial& material = m_resourceManager.GetMaterial(draw.Material);

        if (!*material.DescriptorSet) return;

        if (!m_bound.PipelineEntry || m_bound.Shader != material.Shader || m_bound.Settings != material.Settings || m_bound.Variant != draw.Variant
            || m_bound.Target != target || m_bound.VertexBufferHandle != draw.VertexBuffer)
        {
            m_bound.PipelineEntry = &m_resourceManager.GetOrCreateGraphicsPipeline({
                .Layout = vertexBuffer.Layout, .Shader = material.Shader, .Target = target, .Settings = material.Settings, .Variant = draw.Variant
            });
            m_bound.Shader = material.Shader;
            m_bound.Settings = material.Settings;
            m_bound.Variant = draw.Variant;
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

        // Each shader has its own layout. The frame set and the push constant are the same
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
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, kFrameSpace, *m_inFlight[m_inFlightIndex].DescriptorSet, nullptr);
            m_bound.FrameSetBound = true;
        }

        if (m_bound.MaterialSet != *material.DescriptorSet)
        {
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, kMaterialSpace, *material.DescriptorSet, nullptr);
            m_bound.MaterialSet = *material.DescriptorSet;
            ++m_stats.MaterialBinds;
        }

        const DrawData drawData{ draw.Object, draw.View };

        // A view draws to its own part of the target, like a shadow view to its tile of the atlas
        if (!m_bound.DrawPushed || m_bound.Draw.ViewIndex != drawData.ViewIndex)
            SetViewportAndScissor(cmdBuffer, viewArea);

        if (!m_bound.DrawPushed || m_bound.Draw != drawData)
        {
            cmdBuffer.pushConstants<DrawData>(pipeline.Layout, kDrawDataStages, 0, drawData);
            m_bound.Draw = drawData;
            m_bound.DrawPushed = true;
        }

        const uint32_t total = static_cast<uint32_t>(indexBuffer.Size / Index::Size(indexBuffer.Type));
        const uint32_t indexCount = draw.IndexCount ? draw.IndexCount : total;
        cmdBuffer.drawIndexed(indexCount, 1, draw.FirstIndex, 0, 0);

        ++m_stats.DrawCalls;
        m_stats.Triangles += indexCount / 3;
    }

    bool VulkanRenderer::CanRender() const
    {
        const Window& window = m_device.GetPrimaryWindow();
        return window.Width() > 0 && window.Height() > 0;
    }

    bool VulkanRenderer::Submit(FrameRecorder frame)
    {
        CRIMSON_PROFILE_FUNCTION();

        m_stats = {};

        if (!frame.IsValid() || !CanRender())
            return false;

        const FramePacket& packet = frame.GetPacket();

        if (!BeginFrame(packet))
            return false;

#ifndef NDEBUG
        m_validator.Check(packet, [this](RenderTargetHandle target)
        {
            const VulkanRenderTarget& rt = m_resourceManager.GetRenderTarget(target);
            std::vector<TextureHandle> attachments = rt.ColorAttachments;
            if (rt.DepthAttachment)
                attachments.push_back(rt.DepthAttachment);
            return attachments;
        });
#endif

        const vk::raii::CommandBuffer& cmdBuffer = m_inFlight[m_inFlightIndex].CommandBuffer;
        const std::span<const PassRecord> passes = packet.GetPasses();
        m_stats.Passes = static_cast<uint32_t>(passes.size());

        for (const PassRecord& pass : passes)
            ExecutePass(cmdBuffer, pass, packet.GetViews());

        CRIMSON_PROFILE_PLOT("Draw Calls", m_stats.DrawCalls);

        EndFrame();
        return true;
    }

    bool VulkanRenderer::BeginFrame(const FramePacket& packet)
    {
        VulkanSurface& surface = m_resourceManager.GetRenderSurface();

        if (surface.IsDirty && !m_resourceManager.RecreateSwapchain(surface))
            return false;

        const vk::raii::Device& device = m_device.GetDevice();
        InFlightFrame& frame = m_inFlight[m_inFlightIndex];

        {
            CRIMSON_PROFILE_SCOPE("Wait For Frame Fence");
            ScopedWaitTimer waitTimer(m_stats);
            (void)device.waitForFences(*frame.InFlightFence, VK_TRUE, UINT64_MAX);
        }

        // The slot's last frame is done, so nothing reads its buffers anymore, and nothing has bound its set yet,
        // so they can still grow
        m_gpuData.Build(packet.GetLighting(), packet.GetViews(), m_clock.Tick(std::chrono::steady_clock::now()));

        std::memcpy(frame.FrameBlockBuffer.Mapped(), &m_gpuData.Block, sizeof(FrameBlock));
        UploadStorageBuffer(frame, frame.Lights, kLightsBinding, m_gpuData.Lights.data(), std::span(m_gpuData.Lights).size_bytes());
        UploadStorageBuffer(frame, frame.Shadows, kShadowsBinding, m_gpuData.Shadows.data(), std::span(m_gpuData.Shadows).size_bytes());

        const std::span<const GpuObject> objects = packet.GetObjects();
        UploadStorageBuffer(frame, frame.Objects, kObjectsBinding, objects.data(), objects.size_bytes());
        UploadStorageBuffer(frame, frame.Views, kViewsBinding, m_gpuData.Views.data(), std::span(m_gpuData.Views).size_bytes());

        uint32_t imageIndex = 0;
        vk::Result result = vk::Result::eSuccess;

        try
        {
            CRIMSON_PROFILE_SCOPE("Acquire Swapchain Image");
            ScopedWaitTimer waitTimer(m_stats);
            const auto acquired = surface.Swapchain.acquireNextImage(UINT64_MAX, *frame.ImageAvailableSemaphore);
            result = acquired.result;
            imageIndex = acquired.value;
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Vulkan Renderer] AcquireNextImage failed: {}", error.what());
            return false;
        }

        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            surface.IsDirty = true;
            return false;
        }

        surface.CurrentImageIndex = imageIndex;
        device.resetFences(*frame.InFlightFence);

        frame.CommandBuffer.reset();

        vk::CommandBufferBeginInfo begin{};
        begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        frame.CommandBuffer.begin(begin);
        m_bound = {};

        TracyVkCollect(m_tracyContext, static_cast<VkCommandBuffer>(*frame.CommandBuffer));
        return true;
    }

    void VulkanRenderer::EndFrame()
    {
        InFlightFrame& frame = m_inFlight[m_inFlightIndex];
        const vk::raii::CommandBuffer& cmdBuffer = frame.CommandBuffer;

        cmdBuffer.end();

        VulkanSurface& surface = m_resourceManager.GetRenderSurface();
        uint32_t imageIndex = surface.CurrentImageIndex;

        const vk::Semaphore waitSemaphore = *frame.ImageAvailableSemaphore;
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
            queue.submit(submit, *frame.InFlightFence);
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
        m_inFlightIndex = (m_inFlightIndex + 1) % kMaxFramesInFlight;
    }
}
