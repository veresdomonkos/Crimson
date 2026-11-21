#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "crimson/core/profiler.hpp"
#include <bit>
#include <cstring>
#include <vector>

namespace crimson::vulkan
{
    static void SetViewport(const vk::raii::CommandBuffer& cmdBuffer, const RenderRect& rect)
    {
        cmdBuffer.setViewport(0, vk::Viewport{ static_cast<float>(rect.X), static_cast<float>(rect.Y), static_cast<float>(rect.Width), static_cast<float>(rect.Height), 0.0f, 1.0f });
    }

    static void SetScissor(const vk::raii::CommandBuffer& cmdBuffer, const RenderRect& rect)
    {
        cmdBuffer.setScissor(0, vk::Rect2D{ vk::Offset2D{ rect.X, rect.Y }, vk::Extent2D{ rect.Width, rect.Height } });
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
            resources.DescriptorSet = m_resourceManager.AllocateDescriptorSet(frameLayout);

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
            frameWrite.dstBinding = shader_bindings::kFrameBlock;
            frameWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
            frameWrite.descriptorCount = 1;
            frameWrite.pBufferInfo = &frameBufferInfo;

            device.updateDescriptorSets(frameWrite, nullptr);

            // Every binding has to point at something before the first frame, whatever it ends up holding
            constexpr std::size_t kInitialStorageSize = 16 * 1024;
            UploadStorageBuffer(resources, resources.Lights, shader_bindings::kLights, nullptr, kInitialStorageSize);
            UploadStorageBuffer(resources, resources.Shadows, shader_bindings::kShadows, nullptr, kInitialStorageSize);
            UploadStorageBuffer(resources, resources.Objects, shader_bindings::kObjects, nullptr, kInitialStorageSize);
            UploadStorageBuffer(resources, resources.Views, shader_bindings::kViews, nullptr, kInitialStorageSize);
        }
    }

    // Only call this for a frame whose fence has been waited on, as it can free the buffer the frame used last
    bool VulkanRenderer::GrowFrameBuffer(FrameHostBuffer& buffer, vk::BufferUsageFlagBits usage, vk::DeviceSize size)
    {
        if (size <= buffer.Capacity)
            return true;

        vk::DeviceSize capacity = std::max<vk::DeviceSize>(buffer.Capacity, 1);
        while (capacity < size)
            capacity *= 2;

        AllocatedBuffer created = CreateAllocatedBuffer(m_device.GetAllocator(), capacity, usage, BufferMemory::HostMapped);

        if (!created)
        {
            LOG_ERROR("[Vulkan Renderer] Failed to grow a frame buffer to {} bytes", capacity);
            return false;
        }

        buffer.Buffer = std::move(created);
        buffer.Capacity = capacity;
        return true;
    }

    // Only call this for a frame whose fence has been waited on, as it can free the buffer the frame used last
    void VulkanRenderer::UploadStorageBuffer(const InFlightFrame& resources, FrameHostBuffer& storage, uint32_t binding, const void* data, std::size_t size)
    {
        if (size > storage.Capacity)
        {
            if (!GrowFrameBuffer(storage, vk::BufferUsageFlagBits::eStorageBuffer, size))
                return;

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

    void VulkanRenderer::PreparePropertyResources(InFlightFrame& frame, const MaterialPropertiesBudget& budget)
    {
        // The pool is only made again when this frame needs more than it has, and then for twice that at most
        if (budget.Draws > frame.PropertyPoolSets || budget.Textures > frame.PropertyPoolTextures)
        {
            frame.PropertyPoolSets = std::max(frame.PropertyPoolSets, std::bit_ceil(budget.Draws));
            frame.PropertyPoolTextures = std::max(frame.PropertyPoolTextures, std::bit_ceil(budget.Textures));

            std::array<vk::DescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
            poolSizes[0].descriptorCount = frame.PropertyPoolSets;
            poolSizes[1].type = vk::DescriptorType::eSampledImage;
            poolSizes[1].descriptorCount = frame.PropertyPoolTextures;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = frame.PropertyPoolSets;
            poolInfo.poolSizeCount = 2;
            poolInfo.pPoolSizes = poolSizes.data();

            // The slot's last frame is done, so nothing uses its pool anymore
            frame.PropertyPool = m_device.GetDevice().createDescriptorPool(poolInfo);
        }
        else if (*frame.PropertyPool)
        {
            frame.PropertyPool.reset();
        }

        // Nothing has bound a set of this frame yet, so the buffer can still grow
        GrowFrameBuffer(frame.PropertyBlocks, vk::BufferUsageFlagBits::eUniformBuffer, budget.BlockBytes);
        frame.PropertyCursor = 0;
    }

    vk::DescriptorSet VulkanRenderer::WritePropertySet(InFlightFrame& frame, const FramePacket& packet, const DrawItem& draw, const VulkanMaterial& material)
    {
        const vk::raii::Device& device = m_device.GetDevice();
        const VulkanShader& shader = m_resourceManager.GetShader(material.Shader);
        const std::span<const MaterialPropertyEntry> entries = packet.GetPropertyEntries(draw.Properties);

        // A failed growth of the block buffer leaves it too small, so the draw is skipped like one without a set
        if (shader.Interface.MaterialBlockSize > 0 && SetsValues(entries, shader.Interface) && frame.PropertyCursor + shader.Interface.MaterialBlockSize > frame.PropertyBlocks.Capacity)
        {
            LOG_ERROR("[Vulkan Renderer] The block buffer of a draw with properties is too small");
            return {};
        }

        const vk::DescriptorSetLayout layout = *shader.MaterialSetLayout;
        vk::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = *frame.PropertyPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &layout;

        // Without the raii wrapper, as the sets are not freed one by one, the pool is reset
        VkDescriptorSet raw = VK_NULL_HANDLE;
        const VkResult allocated = device.getDispatcher()->vkAllocateDescriptorSets(
            static_cast<VkDevice>(*device), reinterpret_cast<const VkDescriptorSetAllocateInfo*>(&allocateInfo), &raw);

        // The pool was sized for every draw of the frame, so this is a bug and not a full pool
        if (allocated != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan Renderer] Failed to allocate the material set of a draw with properties");
            return {};
        }

        const vk::DescriptorSet set = raw;
        m_propertyWrites.clear();
        m_propertyImages.clear();
        m_propertyImages.reserve(shader.Interface.Textures.size());

        if (shader.Interface.MaterialBlockSize > 0)
        {
            const vk::DeviceSize size = shader.Interface.MaterialBlockSize;

            if (SetsValues(entries, shader.Interface))
            {
                auto* block = static_cast<std::byte*>(frame.PropertyBlocks.Buffer.Mapped()) + frame.PropertyCursor;

                WriteMaterialBlock({ block, static_cast<std::size_t>(size) }, { static_cast<const std::byte*>(material.UniformBuffer.Mapped()), static_cast<std::size_t>(size) },
                                   shader.Interface, entries, packet.GetPropertyBytes(draw.Properties));

                m_propertyBuffer = vk::DescriptorBufferInfo{ frame.PropertyBlocks.Buffer.Get(), frame.PropertyCursor, size };

                const vk::DeviceSize alignment = std::max<vk::DeviceSize>(m_device.GetProperties().limits.minUniformBufferOffsetAlignment, 1);
                frame.PropertyCursor += (size + alignment - 1) / alignment * alignment;
            }
            else
            {
                m_propertyBuffer = vk::DescriptorBufferInfo{ material.UniformBuffer.Get(), 0, size };
            }

            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = shader.Interface.MaterialBlockBinding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eUniformBuffer;
            write.pBufferInfo = &m_propertyBuffer;
            m_propertyWrites.push_back(write);
        }

        for (const auto& [id, slot] : shader.Interface.Textures)
        {
            const TextureHandle texture = DrawTexture(id, material.Textures, entries);

            m_propertyImages.push_back({ {}, *m_resourceManager.GetTexture(texture ? texture : m_resourceManager.GetDefaultTexture()).View, vk::ImageLayout::eShaderReadOnlyOptimal });

            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = slot.Binding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eSampledImage;
            write.pImageInfo = &m_propertyImages.back();
            m_propertyWrites.push_back(write);
        }

        if (!m_propertyWrites.empty())
            device.updateDescriptorSets(m_propertyWrites, nullptr);

        return set;
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
            write.dstBinding = shader_bindings::kShadowAtlas;
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

    void VulkanRenderer::ExecutePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, const FramePacket& packet)
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

        // What the draws' properties sample has to be readable too, and can't change inside rendering. A texture the UI shows can
        // be one that a pass of this frame rendered to.
        for (const DrawItem& draw : pass.GetDraws())
        {
            if (!draw.Properties)
                continue;

            for (const MaterialPropertyEntry& entry : packet.GetPropertyEntries(draw.Properties))
            {
                if (entry.Texture)
                    TransitionImage(cmdBuffer, m_resourceManager.GetTexture(entry.Texture), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        BeginRendering(cmdBuffer, rt, info.Clear, info.Viewport);

        // BeginRendering set the viewport to the pass's area, which the first draw's view may not have
        m_bound.DrawPushed = false;

        const RenderRect passArea = info.Viewport.value_or(RenderRect{ 0, 0, rt.Width, rt.Height });
        const std::span<const DrawItem> draws = pass.GetDraws();

        BuildDrawOrder(draws, m_resourceManager, m_drawOrder, info.Kind);
        for (const DrawSortEntry& sorted : m_drawOrder)
            ExecuteDraw(cmdBuffer, draws[sorted.Index], target, packet, passArea, info.Kind);

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

        SetViewport(cmdBuffer, rect);
        SetScissor(cmdBuffer, rect);
    }

    void VulkanRenderer::ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawItem& draw, RenderTargetHandle target, const FramePacket& packet, const RenderRect& passArea, PassKind kind)
    {
        CRIMSON_PROFILE_FUNCTION();

        const std::span<const FrameView> views = packet.GetViews();

        // A view whose part of the target is outside the pass's draws nothing
        const RenderRect viewArea = ViewArea(views[draw.View.Index()], passArea);
        if (IsEmpty(viewArea)) return;

        // A draw can limit itself further, to a part of what its view shows
        const RenderRect scissor = draw.Scissor ? Intersect(viewArea, *draw.Scissor) : viewArea;
        if (IsEmpty(scissor)) return;

        VulkanVertexBuffer& vertexBuffer = m_resourceManager.GetVertexBuffer(draw.VertexBuffer);
        VulkanIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(draw.IndexBuffer);
        VulkanMaterial& material = m_resourceManager.GetMaterial(draw.Material);

        if (!*material.DescriptorSet) return;

        if (!m_bound.PipelineEntry || m_bound.Shader != material.Shader || m_bound.Settings != material.Settings || m_bound.Variant != draw.Variant
            || m_bound.Target != target || m_bound.VertexBufferHandle != draw.VertexBuffer || m_bound.Kind != kind)
        {
            m_bound.PipelineEntry = &m_resourceManager.GetOrCreateGraphicsPipeline({
                .Layout = vertexBuffer.Layout, .Shader = material.Shader, .Target = target, .Settings = material.Settings, .Variant = draw.Variant, .Kind = kind
            });
            m_bound.Shader = material.Shader;
            m_bound.Settings = material.Settings;
            m_bound.Variant = draw.Variant;
            m_bound.Target = target;
            m_bound.VertexBufferHandle = draw.VertexBuffer;
            m_bound.Kind = kind;
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
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, shader_bindings::kFrameSpace, *m_inFlight[m_inFlightIndex].DescriptorSet, nullptr);
            m_bound.FrameSetBound = true;
        }

        // A draw with properties has a set of its own, so the next draw binds its own again
        vk::DescriptorSet materialSet = *material.DescriptorSet;

        if (draw.Properties)
        {
            materialSet = WritePropertySet(m_inFlight[m_inFlightIndex], packet, draw, material);

            if (!materialSet)
                return;
        }

        if (m_bound.MaterialSet != materialSet)
        {
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.Layout, shader_bindings::kMaterialSpace, materialSet, nullptr);
            m_bound.MaterialSet = materialSet;
            ++m_stats.MaterialBinds;
        }

        const DrawData drawData{ draw.Object.Index(), draw.View.Index() };

        // A view draws to its own part of the target, like a shadow view to its tile of the atlas
        if (!m_bound.DrawPushed || m_bound.Draw.ViewIndex != drawData.ViewIndex)
            SetViewport(cmdBuffer, viewArea);

        if (!m_bound.DrawPushed || m_bound.Scissor != scissor)
        {
            SetScissor(cmdBuffer, scissor);
            m_bound.Scissor = scissor;
        }

        if (!m_bound.DrawPushed || m_bound.Draw != drawData)
        {
            cmdBuffer.pushConstants<DrawData>(pipeline.Layout, kDrawDataStages, 0, drawData);
            m_bound.Draw = drawData;
            m_bound.DrawPushed = true;
        }

        const uint32_t total = static_cast<uint32_t>(indexBuffer.Size / Index::Size(indexBuffer.Type));
        const uint32_t indexCount = draw.IndexCount ? draw.IndexCount : total;
        cmdBuffer.drawIndexed(indexCount, 1, draw.FirstIndex, draw.BaseVertex, 0);

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
            ExecutePass(cmdBuffer, pass, packet);

        CRIMSON_PROFILE_PLOT("Draw Calls", m_stats.DrawCalls);

        EndFrame();

        // A frame that was dropped has no number, so what it updated is written again by the next one
        m_resourceManager.FinishFrame();
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
        UploadStorageBuffer(frame, frame.Lights, shader_bindings::kLights, m_gpuData.Lights.data(), std::span(m_gpuData.Lights).size_bytes());
        UploadStorageBuffer(frame, frame.Shadows, shader_bindings::kShadows, m_gpuData.Shadows.data(), std::span(m_gpuData.Shadows).size_bytes());

        const std::span<const GpuObject> objects = packet.GetObjects();
        UploadStorageBuffer(frame, frame.Objects, shader_bindings::kObjects, objects.data(), objects.size_bytes());
        UploadStorageBuffer(frame, frame.Views, shader_bindings::kViews, m_gpuData.Views.data(), std::span(m_gpuData.Views).size_bytes());

        PreparePropertyResources(frame, CountMaterialProperties(packet, m_resourceManager, m_device.GetProperties().limits.minUniformBufferOffsetAlignment));

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
