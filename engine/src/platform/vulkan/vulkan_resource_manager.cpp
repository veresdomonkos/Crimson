#include "vulkan_resource_manager.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "utils.hpp"
#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"

namespace crimson::vulkan
{
    VulkanResourceManager::VulkanResourceManager(VulkanDevice& device)
        : m_device(device)
    {
        CreateDescriptorPool();
        CreateSamplers();
        CreateStandardSetLayouts();
        CreateDefaultTexture();
        CreateSwapchainResources(m_primarySurface);
    }

    VulkanResourceManager::~VulkanResourceManager()
    {
        m_device.WaitIdle();

        // The registries live in the base class and would otherwise outlive the pool and layouts below
        DestroyResources();
    }

    void VulkanResourceManager::CreateDescriptorPool()
    {
        // Frame sets take one UBO, four storage buffers, the atlas and four samplers per frame in flight
        std::array<vk::DescriptorPoolSize, 4> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 100 + kMaxFramesInFlight;

        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 4 * kMaxFramesInFlight;

        poolSizes[2].type = vk::DescriptorType::eSampledImage;
        poolSizes[2].descriptorCount = 100 + kMaxFramesInFlight;

        poolSizes[3].type = vk::DescriptorType::eSampler;
        poolSizes[3].descriptorCount = 4 * kMaxFramesInFlight;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 100;
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        m_descriptorPool = m_device.GetDevice().createDescriptorPool(poolInfo);
    }

    void VulkanResourceManager::CreateSamplers()
    {
        const auto create = [this](vk::Filter filter, vk::SamplerAddressMode addressMode, bool compare)
        {
            vk::SamplerCreateInfo createInfo{};
            createInfo.magFilter = filter;
            createInfo.minFilter = filter;
            createInfo.addressModeU = addressMode;
            createInfo.addressModeV = addressMode;
            createInfo.addressModeW = addressMode;
            createInfo.anisotropyEnable = VK_FALSE;
            createInfo.maxAnisotropy = 1.0f;
            createInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
            createInfo.unnormalizedCoordinates = VK_FALSE;
            createInfo.compareEnable = compare ? VK_TRUE : VK_FALSE;
            createInfo.compareOp = compare ? vk::CompareOp::eLessOrEqual : vk::CompareOp::eAlways;
            createInfo.mipmapMode = filter == vk::Filter::eLinear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
            createInfo.minLod = 0.0f;
            createInfo.maxLod = VK_LOD_CLAMP_NONE;

            return m_device.GetDevice().createSampler(createInfo);
        };

        m_linearWrapSampler = create(vk::Filter::eLinear, vk::SamplerAddressMode::eRepeat, false);
        m_linearClampSampler = create(vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, false);
        m_pointClampSampler = create(vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, false);
        m_shadowCompareSampler = create(vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, true);
    }

    void VulkanResourceManager::CreateStandardSetLayouts()
    {
        const vk::ShaderStageFlags allStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        const auto makeBinding = [&](uint32_t binding, vk::DescriptorType type, const vk::Sampler* immutableSampler = nullptr)
        {
            vk::DescriptorSetLayoutBinding result{};
            result.binding = binding;
            result.descriptorType = type;
            result.descriptorCount = 1;
            result.stageFlags = allStages;
            result.pImmutableSamplers = immutableSampler;
            return result;
        };

        const vk::Sampler linearWrap = *m_linearWrapSampler;
        const vk::Sampler linearClamp = *m_linearClampSampler;
        const vk::Sampler pointClamp = *m_pointClampSampler;
        const vk::Sampler shadowCompare = *m_shadowCompareSampler;

        const std::array frameBindings = {
            makeBinding(kFrameBlockBinding, vk::DescriptorType::eUniformBuffer),
            makeBinding(kLightsBinding, vk::DescriptorType::eStorageBuffer),
            makeBinding(kShadowsBinding, vk::DescriptorType::eStorageBuffer),
            makeBinding(kObjectsBinding, vk::DescriptorType::eStorageBuffer),
            makeBinding(kShadowAtlasBinding, vk::DescriptorType::eSampledImage),
            makeBinding(kViewsBinding, vk::DescriptorType::eStorageBuffer),
            makeBinding(kLinearWrapBinding, vk::DescriptorType::eSampler, &linearWrap),
            makeBinding(kLinearClampBinding, vk::DescriptorType::eSampler, &linearClamp),
            makeBinding(kPointClampBinding, vk::DescriptorType::eSampler, &pointClamp),
            makeBinding(kShadowCompareBinding, vk::DescriptorType::eSampler, &shadowCompare),
        };

        vk::DescriptorSetLayoutCreateInfo frameInfo{};
        frameInfo.bindingCount = static_cast<uint32_t>(frameBindings.size());
        frameInfo.pBindings = frameBindings.data();

        m_frameSetLayout = m_device.GetDevice().createDescriptorSetLayout(frameInfo);

        // Space 1 has nothing in it, but a pipeline layout needs a set layout for every space up to the material's
        m_unusedSetLayout = m_device.GetDevice().createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{});
    }

    void VulkanResourceManager::CreateDefaultTexture()
    {
        constexpr uint32_t white = 0xFFFFFFFF;

        const TextureInfo info{ .Width = 1, .Height = 1, .Format = TextureFormat::RGBA8, .Usage = TextureUsage::Sampled };
        m_defaultTexture = CreateTexture(info, &white);
    }

    RenderTargetHandle VulkanResourceManager::GetCurrentBackBuffer() const
    {
        return m_primarySurface.SwapchainTargetHandles[m_primarySurface.CurrentImageIndex];
    }

    bool VulkanResourceManager::RecreateSwapchain(VulkanSurface& surface)
    {
        m_device.WaitIdle();
        DestroySwapchainResources(surface);
        return CreateSwapchainResources(surface);
    }

    AllocatedBuffer VulkanResourceManager::CreateBufferWithData(vk::DeviceSize size, vk::BufferUsageFlags usage, BufferUsage bufferUsage, const void* data) const
    {
        const VmaAllocator allocator = m_device.GetAllocator();

        if (bufferUsage == BufferUsage::Static && data)
        {
            AllocatedBuffer staging = CreateAllocatedBuffer(allocator, size, vk::BufferUsageFlagBits::eTransferSrc, BufferMemory::HostMapped);

            if (!staging)
                return {};

            std::memcpy(staging.Mapped(), data, static_cast<size_t>(size));

            AllocatedBuffer buffer = CreateAllocatedBuffer(allocator, size, usage | vk::BufferUsageFlagBits::eTransferDst, BufferMemory::DeviceLocal);

            if (buffer)
                CopyBuffer(staging.Get(), buffer.Get(), size);

            return buffer;
        }

        AllocatedBuffer buffer = CreateAllocatedBuffer(allocator, size, usage, BufferMemory::HostMapped);

        if (buffer && data)
            std::memcpy(buffer.Mapped(), data, static_cast<size_t>(size));

        return buffer;
    }

    VertexBufferHandle VulkanResourceManager::CreateVertexBuffer(const VertexBufferInfo& info, const void* data)
    {
        VulkanVertexBuffer buffer{};
        buffer.Size = info.Size;
        buffer.Layout = info.Layout;
        buffer.Usage = info.Usage;
        buffer.Buffer = CreateBufferWithData(info.Size, vk::BufferUsageFlagBits::eVertexBuffer, info.Usage, data);

        if (!buffer.Buffer)
            return {};

        return m_vertexBuffers.Register(std::move(buffer));
    }

    void VulkanResourceManager::DestroyVertexBuffer(VertexBufferHandle handle)
    {
        m_device.WaitIdle();
        m_vertexBuffers.Unregister(handle);
    }

    IndexBufferHandle VulkanResourceManager::CreateIndexBuffer(const IndexBufferInfo& info, const void* data)
    {
        VulkanIndexBuffer buffer{};
        buffer.Size = info.Size;
        buffer.Type = info.Type;
        buffer.Usage = info.Usage;
        buffer.Buffer = CreateBufferWithData(info.Size, vk::BufferUsageFlagBits::eIndexBuffer, info.Usage, data);

        if (!buffer.Buffer)
            return {};

        return m_indexBuffers.Register(std::move(buffer));
    }

    void VulkanResourceManager::DestroyIndexBuffer(IndexBufferHandle handle)
    {
        m_device.WaitIdle();
        m_indexBuffers.Unregister(handle);
    }

    ShaderHandle VulkanResourceManager::CreateShader(const ShaderBinary& binary)
    {
        auto shaderInterface = ReflectShaderInterface(binary);
        if (!shaderInterface)
            return {};

        try
        {
            VulkanShader shader{};

            shader.Vertex = CreateShaderModule(binary.Vertex);
            shader.Fragment = CreateShaderModule(binary.Pixel);
            if (!binary.PixelDepth.empty())
                shader.FragmentDepth = CreateShaderModule(binary.PixelDepth);
            shader.Interface = std::move(*shaderInterface);

            const auto stageFlags = [](bool vertex, bool pixel)
            {
                vk::ShaderStageFlags flags{};
                if (vertex) flags |= vk::ShaderStageFlagBits::eVertex;
                if (pixel) flags |= vk::ShaderStageFlagBits::eFragment;
                return flags;
            };

            std::vector<vk::DescriptorSetLayoutBinding> bindings;

            if (shader.Interface.MaterialBlockSize > 0)
            {
                vk::DescriptorSetLayoutBinding binding{};
                binding.binding = shader.Interface.MaterialBlockBinding;
                binding.descriptorCount = 1;
                binding.descriptorType = vk::DescriptorType::eUniformBuffer;
                binding.stageFlags = stageFlags(shader.Interface.MaterialBlockInVertex, shader.Interface.MaterialBlockInPixel);
                bindings.push_back(binding);
            }

            for (const auto& [name, texture] : shader.Interface.Textures)
            {
                vk::DescriptorSetLayoutBinding binding{};
                binding.binding = texture.Binding;
                binding.descriptorCount = 1;
                binding.descriptorType = vk::DescriptorType::eSampledImage;
                binding.stageFlags = stageFlags(texture.Vertex, texture.Pixel);
                bindings.push_back(binding);
            }

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

            shader.MaterialSetLayout = m_device.GetDevice().createDescriptorSetLayout(layoutInfo);

            // Shared by all of the shader's pipelines, so switching between them keeps its material bound.
            // The frame set and the push constant are the same in every shader, so they stay valid across shaders.
            const std::array<vk::DescriptorSetLayout, 3> setLayouts = { *m_frameSetLayout, *m_unusedSetLayout, *shader.MaterialSetLayout };

            vk::PushConstantRange pushConstant{};
            pushConstant.stageFlags = kDrawDataStages;
            pushConstant.offset = 0;
            pushConstant.size = sizeof(DrawData);

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            pipelineLayoutInfo.pSetLayouts = setLayouts.data();
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

            shader.PipelineLayout = m_device.GetDevice().createPipelineLayout(pipelineLayoutInfo);

            return m_shaders.Register(std::move(shader));
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Vulkan] Failed to create shader: {}", error.what());
            return {};
        }
    }

    void VulkanResourceManager::DestroyShader(ShaderHandle handle)
    {
        m_device.WaitIdle();
        m_shaders.Unregister(handle);
    }

    MaterialHandle VulkanResourceManager::CreateMaterial(ShaderHandle shaderHandle, const MaterialSettings& settings)
    {
        VulkanShader* shader = m_shaders.Find(shaderHandle);

        if (!shader)
            return {};

        try
        {
            VulkanMaterial material{};
            material.Shader = shaderHandle;
            material.Settings = settings;
            material.UniformBufferSize = shader->Interface.MaterialBlockSize;

            const vk::DescriptorSetLayout setLayout = *shader->MaterialSetLayout;

            vk::DescriptorSetAllocateInfo allocateInfo{};
            allocateInfo.descriptorPool = *m_descriptorPool;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts = &setLayout;

            material.DescriptorSet = std::move(m_device.GetDevice().allocateDescriptorSets(allocateInfo).front());

            if (material.UniformBufferSize > 0)
            {
                material.UniformBuffer = CreateAllocatedBuffer(m_device.GetAllocator(), material.UniformBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, BufferMemory::HostMapped);

                if (!material.UniformBuffer)
                    return {};

                vk::DescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = material.UniformBuffer.Get();
                bufferInfo.offset = 0;
                bufferInfo.range = material.UniformBufferSize;

                vk::WriteDescriptorSet write{};
                write.dstSet = *material.DescriptorSet;
                write.dstBinding = shader->Interface.MaterialBlockBinding;
                write.descriptorCount = 1;
                write.descriptorType = vk::DescriptorType::eUniformBuffer;
                write.pBufferInfo = &bufferInfo;

                m_device.GetDevice().updateDescriptorSets(write, nullptr);
            }

            for (const auto& [name, texture] : shader->Interface.Textures)
                WriteMaterialTexture(*material.DescriptorSet, texture.Binding, m_textures.Get(m_defaultTexture));

            return m_materials.Register(std::move(material));
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Vulkan] Failed to create material: {}", error.what());
            return {};
        }
    }

    void VulkanResourceManager::DestroyMaterial(MaterialHandle handle)
    {
        m_device.WaitIdle();
        m_materials.Unregister(handle);
    }

    void VulkanResourceManager::SetMaterialTexture(MaterialHandle handle, std::string_view name, TextureHandle textureHandle)
    {
        VulkanMaterial& material = m_materials.Get(handle);
        VulkanShader& shader = m_shaders.Get(material.Shader);
        VulkanTexture& texture = m_textures.Get(textureHandle ? textureHandle : m_defaultTexture);

        auto it = shader.Interface.Textures.find(std::string(name));

        if (it == shader.Interface.Textures.end())
        {
            LOG_WARN("[Vulkan] Texture property '{}' not found in shader", name);
            return;
        }

        // Frames still in flight may be using this set
        m_device.WaitIdle();

        WriteMaterialTexture(*material.DescriptorSet, it->second.Binding, texture);
    }

    void VulkanResourceManager::WriteMaterialTexture(vk::DescriptorSet set, uint32_t binding, const VulkanTexture& texture) const
    {
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageView = *texture.View;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eSampledImage;
        write.pImageInfo = &imageInfo;

        m_device.GetDevice().updateDescriptorSets(write, nullptr);
    }

    TextureHandle VulkanResourceManager::CreateTexture(const TextureInfo& info, const void* data)
    {
        const vk::Format format = utils::GetVkFormat(info.Format);

        if (format == vk::Format::eUndefined)
            return TextureHandle::Invalid();

        vk::ImageUsageFlags usage{};

        if (HasTextureUsage(info.Usage, TextureUsage::Sampled))
            usage |= vk::ImageUsageFlagBits::eSampled;

        // Transfer source so that ReadColorPixels can read it
        if (HasTextureUsage(info.Usage, TextureUsage::ColorAttachment))
            usage |= vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;

        if (HasTextureUsage(info.Usage, TextureUsage::DepthStencilAttachment))
            usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;

        if (!usage)
            return TextureHandle::Invalid();

        VulkanTexture texture{};
        texture.Format = format;
        texture.Width = info.Width;
        texture.Height = info.Height;
        texture.Layout = vk::ImageLayout::eUndefined;
        texture.Aspect = utils::GetImageAspect(info.Format);

        if (data)
            usage |= vk::ImageUsageFlagBits::eTransferDst;

        // Each mip level is made by blitting from the one above it
        if (data && info.MipLevels > 1)
            usage |= vk::ImageUsageFlagBits::eTransferSrc;

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = format;
        imageInfo.extent = vk::Extent3D{ info.Width, info.Height, 1 };
        imageInfo.mipLevels = info.MipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = usage;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        const bool isAttachment = static_cast<bool>(usage & (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment));

        texture.Allocation = CreateAllocatedImage(m_device.GetAllocator(), imageInfo, isAttachment);

        if (!texture.Allocation)
            return TextureHandle::Invalid();

        texture.Image = texture.Allocation.Get();

        try
        {
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = texture.Image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = format;
            // Descriptors can only read one aspect, so sampling goes through a depth-only view
            viewInfo.subresourceRange.aspectMask = (texture.Aspect & vk::ImageAspectFlagBits::eDepth) ? vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth) : texture.Aspect;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = info.MipLevels;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            texture.View = m_device.GetDevice().createImageView(viewInfo);

            if (utils::HasStencilComponent(format))
            {
                viewInfo.subresourceRange.aspectMask = texture.Aspect;
                texture.AttachmentView = m_device.GetDevice().createImageView(viewInfo);
            }
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Vulkan] Failed to create image view: {}", error.what());
            return TextureHandle::Invalid();
        }

        TextureHandle handle = m_textures.Register(std::move(texture));

        if (data)
            UploadTextureData(m_textures.Get(handle), info, data);

        return handle;
    }

    void VulkanResourceManager::DestroyTexture(TextureHandle handle)
    {
        m_device.WaitIdle();
        m_textures.Unregister(handle);
    }

    std::vector<std::byte> VulkanResourceManager::ReadColorPixels(TextureHandle handle)
    {
        constexpr uint32_t kBytesPerPixel = 4;

        // The frames still in flight may be writing to it
        m_device.WaitIdle();

        const VulkanTexture& texture = m_textures.Get(handle);

        if (texture.Format != vk::Format::eR8G8B8A8Unorm && texture.Format != vk::Format::eR8G8B8A8Srgb)
        {
            LOG_ERROR("[Vulkan] Only RGBA8 textures can be read back");
            return {};
        }

        const vk::DeviceSize size = static_cast<vk::DeviceSize>(texture.Width) * texture.Height * kBytesPerPixel;

        AllocatedBuffer staging = CreateAllocatedBuffer(m_device.GetAllocator(), size, vk::BufferUsageFlagBits::eTransferDst, BufferMemory::HostReadable);

        if (!staging)
        {
            LOG_ERROR("[Vulkan] Failed to create a buffer to read a texture into");
            return {};
        }

        m_device.ImmediateSubmit([&](vk::CommandBuffer commandBuffer)
        {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.oldLayout = texture.Layout;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture.Image;
            barrier.subresourceRange = vk::ImageSubresourceRange{ texture.Aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };

            vk::DependencyInfo dependency{};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            commandBuffer.pipelineBarrier2(dependency);

            vk::BufferImageCopy region{};
            region.imageSubresource.aspectMask = texture.Aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = vk::Extent3D{ texture.Width, texture.Height, 1 };
            commandBuffer.copyImageToBuffer(texture.Image, vk::ImageLayout::eTransferSrcOptimal, staging.Get(), region);

            // Back to how the renderer left it, as it tracks the layout
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead;
            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = texture.Layout == vk::ImageLayout::eUndefined ? vk::ImageLayout::eTransferSrcOptimal : texture.Layout;
            commandBuffer.pipelineBarrier2(dependency);

            vk::BufferMemoryBarrier2 hostBarrier{};
            hostBarrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            hostBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            hostBarrier.dstStageMask = vk::PipelineStageFlagBits2::eHost;
            hostBarrier.dstAccessMask = vk::AccessFlagBits2::eHostRead;
            hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hostBarrier.buffer = staging.Get();
            hostBarrier.size = size;

            vk::DependencyInfo hostDependency{};
            hostDependency.bufferMemoryBarrierCount = 1;
            hostDependency.pBufferMemoryBarriers = &hostBarrier;
            commandBuffer.pipelineBarrier2(hostDependency);
        });

        std::vector<std::byte> pixels(static_cast<size_t>(size));
        std::memcpy(pixels.data(), staging.Mapped(), pixels.size());
        return pixels;
    }

    RenderTargetHandle VulkanResourceManager::CreateRenderTarget(const RenderTargetInfo& info)
    {
        VulkanRenderTarget target{};
        target.Width = info.Width;
        target.Height = info.Height;
        target.IsSwapchain = false;

        for (const auto& format : info.ColorFormats)
        {
            TextureInfo textureInfo{};
            textureInfo.Width = info.Width;
            textureInfo.Height = info.Height;
            textureInfo.Format = format;
            textureInfo.Usage =  TextureUsage::ColorAttachment | TextureUsage::Sampled;

            TextureHandle texture = CreateTexture(textureInfo, nullptr);

            if (!texture)
            {
                for (TextureHandle attachment : target.ColorAttachments)
                    DestroyTexture(attachment);

                return {};
            }

            target.ColorAttachments.push_back(texture);
        }

        if (info.DepthFormat)
        {
            TextureInfo depthInfo{};
            depthInfo.Width = info.Width;
            depthInfo.Height = info.Height;
            depthInfo.Format = info.DepthFormat.value();
            depthInfo.Usage =  TextureUsage::DepthStencilAttachment | TextureUsage::Sampled;

            TextureHandle depth = CreateTexture(depthInfo, nullptr);

            if (!depth)
            {
                for (TextureHandle attachment : target.ColorAttachments)
                    DestroyTexture(attachment);

                return RenderTargetHandle::Invalid();
            }

            target.DepthAttachment = depth;
        }

        return m_renderTargets.Register(std::move(target));
    }

    void VulkanResourceManager::DestroyRenderTarget(RenderTargetHandle handle)
    {
        m_device.WaitIdle();

        VulkanRenderTarget* target = m_renderTargets.Find(handle);

        if (!target)
            return;

        for (TextureHandle texture : target->ColorAttachments)
            DestroyTexture(texture);

        if (target->DepthAttachment)
            DestroyTexture(target->DepthAttachment);

        m_renderTargets.Unregister(handle);
    }

    TextureHandle VulkanResourceManager::GetColorAttachment(RenderTargetHandle handle, uint32_t index) const
    {
        return m_renderTargets.Get(handle).ColorAttachments[index];
    }

    std::optional<TextureHandle> VulkanResourceManager::GetDepthAttachment(RenderTargetHandle handle) const
    {
        return m_renderTargets.Get(handle).DepthAttachment;
    }

    VulkanGraphicsPipeline& VulkanResourceManager::GetOrCreateGraphicsPipeline(const GraphicsPipelineInfo& info)
    {
        auto [it, inserted] = m_graphicsPipelines.try_emplace(info);

        if (inserted)
            it->second = CreateGraphicsPipeline(info);

        return it->second;
    }

    VulkanGraphicsPipeline VulkanResourceManager::CreateGraphicsPipeline(const GraphicsPipelineInfo& info)
    {
        const VulkanShader& shader = GetShader(info.Shader);

        if (!*shader.Vertex || !*shader.Fragment)
        {
            LOG_ERROR("[Renderer] Cannot create Vulkan pipeline without valid shader modules");
            return {};
        }

        const VulkanRenderTarget& target = GetRenderTarget(info.Target);

        std::vector<vk::Format> colorFormats;
        colorFormats.reserve(target.ColorAttachments.size());
        for (TextureHandle colorHandle : target.ColorAttachments)
        {
            colorFormats.push_back(GetTexture(colorHandle).Format);
        }

        const bool hasDepth = static_cast<bool>(target.DepthAttachment);
        const vk::Format depthFormat = hasDepth ? GetTexture(target.DepthAttachment).Format : vk::Format::eUndefined;

        // A depth pass uses PSDepth, or no fragment stage at all, as depth comes from the vertex shader
        const bool depthPass = info.Variant == ShaderVariant::Depth;
        const vk::ShaderModule fragment = depthPass ? *shader.FragmentDepth : *shader.Fragment;

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{};
        shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
        shaderStages[0].module = *shader.Vertex;
        shaderStages[0].pName = "main";

        shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
        shaderStages[1].module = fragment;
        shaderStages[1].pName = "main";

        const uint32_t stageCount = fragment ? 2u : 1u;

        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = info.Layout.GetStride();
        binding.inputRate = vk::VertexInputRate::eVertex;

        std::vector<vk::VertexInputAttributeDescription> attributes;
        uint32_t location = 0;
        for (const auto& element : info.Layout)
        {
            vk::VertexInputAttributeDescription attribute{};
            attribute.location = location++;
            attribute.binding = 0;
            switch (element.Type)
            {
                case ShaderDataType::Float:  attribute.format = vk::Format::eR32Sfloat; break;
                case ShaderDataType::Float2: attribute.format = vk::Format::eR32G32Sfloat; break;
                case ShaderDataType::Float3: attribute.format = vk::Format::eR32G32B32Sfloat; break;
                case ShaderDataType::Float4: attribute.format = vk::Format::eR32G32B32A32Sfloat; break;
                case ShaderDataType::Int:    attribute.format = vk::Format::eR32Sint; break;
                case ShaderDataType::Int2:   attribute.format = vk::Format::eR32G32Sint; break;
                case ShaderDataType::Int3:   attribute.format = vk::Format::eR32G32B32Sint; break;
                case ShaderDataType::Int4:   attribute.format = vk::Format::eR32G32B32A32Sint; break;
            }
            attribute.offset = element.Offset;
            attributes.push_back(attribute);
        }

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &binding;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterInfo{};
        rasterInfo.depthClampEnable = VK_FALSE;
        rasterInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterInfo.polygonMode = vk::PolygonMode::eFill;
        rasterInfo.lineWidth = 1.0f;
        switch (info.Settings.Cull)
        {
            case CullMode::None:  rasterInfo.cullMode = vk::CullModeFlagBits::eNone; break;
            case CullMode::Back:  rasterInfo.cullMode = vk::CullModeFlagBits::eBack; break;
            case CullMode::Front: rasterInfo.cullMode = vk::CullModeFlagBits::eFront; break;
        }
        rasterInfo.frontFace = vk::FrontFace::eClockwise;
        rasterInfo.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthState{};
        depthState.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
        // A depth pass draws everything as opaque, so a transparent mesh still casts its shadow
        const bool isTransparent = !depthPass && info.Settings.RenderMode == MaterialRenderMode::Transparent;

        depthState.depthWriteEnable = hasDepth && !isTransparent ? VK_TRUE : VK_FALSE;
        depthState.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthState.depthBoundsTestEnable = VK_FALSE;
        depthState.stencilTestEnable = VK_FALSE;

        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size());
        for (auto& attachment : colorBlendAttachments)
        {
            attachment.colorWriteMask =
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            attachment.blendEnable = isTransparent ? VK_TRUE : VK_FALSE;

            if (isTransparent)
            {
                // The alpha factors keep an opaque target's alpha at 1, so the viewport doesn't show through
                attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
                attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
                attachment.colorBlendOp = vk::BlendOp::eAdd;
                attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
                attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
                attachment.alphaBlendOp = vk::BlendOp::eAdd;
            }
        }

        vk::PipelineColorBlendStateCreateInfo colorBlendInfo{};
        colorBlendInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlendInfo.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();

        std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VulkanGraphicsPipeline pipeline{};
        pipeline.Layout = *shader.PipelineLayout;

        try
        {
            vk::PipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.viewMask = 0;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
            renderingInfo.pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data();
            renderingInfo.depthAttachmentFormat = depthFormat;
            renderingInfo.stencilAttachmentFormat = utils::HasStencilComponent(depthFormat) ? depthFormat : vk::Format::eUndefined;

            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.pNext = &renderingInfo;
            pipelineInfo.stageCount = stageCount;
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterInfo;
            pipelineInfo.pMultisampleState = &multisampleState;
            pipelineInfo.pDepthStencilState = &depthState;
            pipelineInfo.pColorBlendState = &colorBlendInfo;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipeline.Layout;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineIndex = -1;

            pipeline.Pipeline = m_device.GetDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Renderer] Failed to create Vulkan graphics pipeline: {}", error.what());
            return {};
        }

        return pipeline;
    }

    void VulkanResourceManager::SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data)
    {
        VulkanMaterial& material = m_materials.Get(handle);
        VulkanShader& shader = m_shaders.Get(material.Shader);

        auto property = shader.Interface.Properties.find(std::string(name));

        if (property == shader.Interface.Properties.end())
        {
            LOG_WARN("[Vulkan] Uniform property '{}' not found in shader", name);
            return;
        }

        if (data.size() > property->second.Size)
        {
            LOG_WARN("[Vulkan] Data size ({}) exceeds property size ({}) for '{}'", data.size(), property->second.Size, name);
            return;
        }

        if (!material.UniformBuffer)
            return;

        std::memcpy(static_cast<std::byte*>(material.UniformBuffer.Mapped()) + property->second.Offset, data.data(), data.size());
    }

    void VulkanResourceManager::CopyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) const
    {
        m_device.ImmediateSubmit([&](vk::CommandBuffer commandBuffer)
        {
            vk::BufferCopy copy{};
            copy.size = size;
            commandBuffer.copyBuffer(src, dst, copy);
        });
    }

    void VulkanResourceManager::UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const
    {
        const vk::DeviceSize size = static_cast<vk::DeviceSize>(info.Width) * info.Height * utils::GetBytesPerPixel(info.Format);

        AllocatedBuffer staging = CreateAllocatedBuffer(m_device.GetAllocator(), size, vk::BufferUsageFlagBits::eTransferSrc, BufferMemory::HostMapped);

        if (!staging)
            return;

        std::memcpy(staging.Mapped(), data, static_cast<size_t>(size));

        m_device.ImmediateSubmit([&](vk::CommandBuffer commandBuffer)
        {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture.Image;
            barrier.subresourceRange = vk::ImageSubresourceRange{ texture.Aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };

            vk::DependencyInfo dependency{};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            commandBuffer.pipelineBarrier2(dependency);

            vk::BufferImageCopy region{};
            region.imageSubresource.aspectMask = texture.Aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = vk::Extent3D{ info.Width, info.Height, 1 };
            commandBuffer.copyBufferToImage(staging.Get(), texture.Image, vk::ImageLayout::eTransferDstOptimal, region);

            const auto transitionLevel = [&](uint32_t level, vk::ImageLayout from, vk::ImageLayout to,
                vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess, vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess)
            {
                barrier.srcStageMask = srcStage;
                barrier.srcAccessMask = srcAccess;
                barrier.dstStageMask = dstStage;
                barrier.dstAccessMask = dstAccess;
                barrier.oldLayout = from;
                barrier.newLayout = to;
                barrier.subresourceRange = vk::ImageSubresourceRange{ texture.Aspect, level, 1, 0, 1 };
                commandBuffer.pipelineBarrier2(dependency);
            };

            int32_t width = static_cast<int32_t>(info.Width);
            int32_t height = static_cast<int32_t>(info.Height);

            for (uint32_t level = 1; level < info.MipLevels; ++level)
            {
                transitionLevel(level - 1, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                    level == 1 ? vk::PipelineStageFlagBits2::eCopy : vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead);

                const int32_t nextWidth = std::max(width / 2, 1);
                const int32_t nextHeight = std::max(height / 2, 1);

                vk::ImageBlit blit{};
                blit.srcSubresource = vk::ImageSubresourceLayers{ texture.Aspect, level - 1, 0, 1 };
                blit.srcOffsets[1] = vk::Offset3D{ width, height, 1 };
                blit.dstSubresource = vk::ImageSubresourceLayers{ texture.Aspect, level, 0, 1 };
                blit.dstOffsets[1] = vk::Offset3D{ nextWidth, nextHeight, 1 };

                commandBuffer.blitImage(texture.Image, vk::ImageLayout::eTransferSrcOptimal, texture.Image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

                transitionLevel(level - 1, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                    vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);

                width = nextWidth;
                height = nextHeight;
            }

            const uint32_t lastLevel = info.MipLevels > 0 ? info.MipLevels - 1 : 0;

            transitionLevel(lastLevel, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                lastLevel == 0 ? vk::PipelineStageFlagBits2::eCopy : vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        });

        texture.Layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    TextureHandle VulkanResourceManager::WrapSwapchainImage(vk::Image image, vk::Format format, uint32_t width, uint32_t height)
    {
        VulkanTexture texture{};
        texture.Image = image;
        texture.Format = format;
        texture.Width = width;
        texture.Height = height;
        texture.Aspect = vk::ImageAspectFlagBits::eColor;
        texture.Layout = vk::ImageLayout::eUndefined;

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = vk::ImageSubresourceRange{ texture.Aspect, 0, 1, 0, 1 };

        texture.View = m_device.GetDevice().createImageView(viewInfo);

        return m_textures.Register(std::move(texture));
    }

    RenderTargetHandle VulkanResourceManager::CreateSwapchainRenderTarget(uint32_t width, uint32_t height, vk::Format colorFormat, vk::Image swapchainImage)
    {
        VulkanRenderTarget target{};
        target.Width = width;
        target.Height = height;
        target.IsSwapchain = true;
        target.ColorAttachments.push_back(WrapSwapchainImage(swapchainImage, colorFormat, width, height));

        return m_renderTargets.Register(std::move(target));
    }

    bool VulkanResourceManager::CreateSwapchainResources(VulkanSurface& surface)
    {
        const vk::raii::PhysicalDevice& physicalDevice = m_device.GetPhysicalDevice();
        const vk::SurfaceKHR windowSurface = m_device.GetSurface();

        try
        {
            const vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(windowSurface);
            const std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(windowSurface);

            if (formats.empty())
                return false;

            vk::SurfaceFormatKHR surfaceFormat = formats[0];

            for (const vk::SurfaceFormatKHR& format : formats)
            {
                if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                {
                    surfaceFormat = format;
                    break;
                }
            }

            const std::vector<vk::PresentModeKHR> presentModes = physicalDevice.getSurfacePresentModesKHR(windowSurface);

            const auto isSupported = [&](vk::PresentModeKHR mode)
            {
                return std::find(presentModes.begin(), presentModes.end(), mode) != presentModes.end();
            };

            // FIFO is the only mode the spec guarantees
            vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;

            if (!std::getenv("CRIMSON_VSYNC"))
            {
                if (isSupported(vk::PresentModeKHR::eMailbox))
                    presentMode = vk::PresentModeKHR::eMailbox;
                else if (isSupported(vk::PresentModeKHR::eImmediate))
                    presentMode = vk::PresentModeKHR::eImmediate;
            }

            const vk::Extent2D extent = capabilities.currentExtent;

            if (extent.width == 0 || extent.height == 0 || extent.width == UINT32_MAX)
                return false;

            uint32_t imageCount = std::max(capabilities.minImageCount + 1, static_cast<uint32_t>(kMaxFramesInFlight));

            if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
                imageCount = capabilities.maxImageCount;

            vk::SwapchainCreateInfoKHR createInfo{};
            createInfo.surface = windowSurface;
            createInfo.minImageCount = imageCount;
            createInfo.imageFormat = surfaceFormat.format;
            createInfo.imageColorSpace = surfaceFormat.colorSpace;
            createInfo.imageExtent = extent;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
            createInfo.imageSharingMode = vk::SharingMode::eExclusive;
            createInfo.preTransform = capabilities.currentTransform;
            createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            createInfo.presentMode = presentMode;
            createInfo.clipped = VK_TRUE;

            surface.Swapchain = m_device.GetDevice().createSwapchainKHR(createInfo);

            LOG_INFO("[Vulkan] Swapchain: {} present mode, {} images requested, {}x{}", vk::to_string(presentMode), imageCount, extent.width, extent.height);

            surface.Extent = extent;
            surface.Format = surfaceFormat.format;

            const std::vector<vk::Image> images = surface.Swapchain.getImages();

            surface.SwapchainTargetHandles.reserve(images.size());
            surface.RenderFinishedSemaphores.reserve(images.size());

            for (vk::Image image : images)
            {
                surface.SwapchainTargetHandles.push_back(CreateSwapchainRenderTarget(extent.width, extent.height, surface.Format, image));
                surface.RenderFinishedSemaphores.push_back(m_device.GetDevice().createSemaphore({}));
            }
        }
        catch (const vk::SystemError& error)
        {
            LOG_ERROR("[Vulkan] Failed to create swapchain: {}", error.what());
            DestroySwapchainResources(surface);
            return false;
        }

        surface.CurrentImageIndex = 0;
        surface.IsDirty = false;
        return true;
    }

    void VulkanResourceManager::DestroySwapchainResources(VulkanSurface& surface)
    {
        for (RenderTargetHandle handle : surface.SwapchainTargetHandles)
            DestroyRenderTarget(handle);

        surface.SwapchainTargetHandles.clear();
        surface.RenderFinishedSemaphores.clear();
        surface.Swapchain.clear();
    }

    vk::raii::ShaderModule VulkanResourceManager::CreateShaderModule(std::span<const uint32_t> code) const
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = code.size_bytes();
        createInfo.pCode = code.data();

        return m_device.GetDevice().createShaderModule(createInfo);
    }

    void VulkanResourceManager::DestroyResources()
    {
        m_graphicsPipelines.clear();

        DestroySwapchainResources(m_primarySurface);

        for (auto handle : m_materials.GetHandles())
            DestroyMaterial(handle);

        for (auto handle : m_renderTargets.GetHandles())
            DestroyRenderTarget(handle);

        for (auto handle : m_textures.GetHandles())
            DestroyTexture(handle);

        for (auto handle : m_vertexBuffers.GetHandles())
            DestroyVertexBuffer(handle);

        for (auto handle : m_indexBuffers.GetHandles())
            DestroyIndexBuffer(handle);

        for (auto handle : m_shaders.GetHandles())
            DestroyShader(handle);
    }
}
