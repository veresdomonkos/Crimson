#include "vulkan_resource_manager.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include <spirv_reflect.h>

#include "utils.hpp"
#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "crimson/renderer/binding_conventions.hpp"

namespace crimson::vulkan
{
    VulkanResourceManager::VulkanResourceManager(VulkanDevice& device)
        : m_device(device)
    {
        CreateDescriptorPool();
        CreateGlobalSetLayout();
        CreateDefaultSampler();
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
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eUniformBufferDynamic;
        poolSizes[0].descriptorCount = 2;

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 100;

        poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[2].descriptorCount = 100;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 100;
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        m_descriptorPool = m_device.GetDevice().createDescriptorPool(poolInfo);
    }

    void VulkanResourceManager::CreateGlobalSetLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        bindings[0].binding = kCameraBlockBinding;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;

        bindings[1].binding = kLightingBlockBinding;
        bindings[1].descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[2].binding = kShadowMapBinding;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo createInfo{};
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        m_globalSetLayout = m_device.GetDevice().createDescriptorSetLayout(createInfo);
    }

    void VulkanResourceManager::CreateDefaultSampler()
    {
        vk::SamplerCreateInfo createInfo{};
        createInfo.magFilter = vk::Filter::eLinear;
        createInfo.minFilter = vk::Filter::eLinear;
        createInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        createInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        createInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        createInfo.anisotropyEnable = VK_FALSE;
        createInfo.maxAnisotropy = 1.0f;
        createInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        createInfo.unnormalizedCoordinates = VK_FALSE;
        createInfo.compareEnable = VK_FALSE;
        createInfo.compareOp = vk::CompareOp::eAlways;
        createInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        createInfo.minLod = 0.0f;
        createInfo.maxLod = VK_LOD_CLAMP_NONE;

        m_defaultSampler = m_device.GetDevice().createSampler(createInfo);
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

    ShaderHandle VulkanResourceManager::CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary)
    {
        try
        {
            VulkanShader shader{};

            shader.Vertex = CreateShaderModule(vertexBinary);
            shader.Fragment = CreateShaderModule(fragmentBinary);

            ReflectShader(shader, vertexBinary);
            ReflectShader(shader, fragmentBinary);

            std::vector<vk::DescriptorSetLayoutBinding> bindings;

            if (shader.MaterialUniformSize > 0)
            {
                vk::DescriptorSetLayoutBinding binding{};
                binding.binding = shader.MaterialUboBinding;
                binding.descriptorCount = 1;
                binding.descriptorType = vk::DescriptorType::eUniformBuffer;
                binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
                bindings.push_back(binding);
            }

            for (const auto& [name, texture] : shader.TextureBindings)
            {
                vk::DescriptorSetLayoutBinding binding{};
                binding.binding = texture.Binding;
                binding.descriptorCount = 1;
                binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
                bindings.push_back(binding);
            }

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

            shader.MaterialSetLayout = m_device.GetDevice().createDescriptorSetLayout(layoutInfo);

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
            material.UniformBufferSize = shader->MaterialUniformSize;

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
                write.dstBinding = shader->MaterialUboBinding;
                write.descriptorCount = 1;
                write.descriptorType = vk::DescriptorType::eUniformBuffer;
                write.pBufferInfo = &bufferInfo;

                m_device.GetDevice().updateDescriptorSets(write, nullptr);
            }

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
        VulkanTexture& texture = m_textures.Get(textureHandle);

        auto it = shader.TextureBindings.find(std::string(name));

        if (it == shader.TextureBindings.end())
            return;

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = *m_defaultSampler;
        imageInfo.imageView = *texture.View;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = *material.DescriptorSet;
        write.dstBinding = it->second.Binding;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
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

        if (HasTextureUsage(info.Usage, TextureUsage::ColorAttachment))
            usage |= vk::ImageUsageFlagBits::eColorAttachment;

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

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{};
        shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
        shaderStages[0].module = *shader.Vertex;
        shaderStages[0].pName = "main";

        shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
        shaderStages[1].module = *shader.Fragment;
        shaderStages[1].pName = "main";

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
        rasterInfo.cullMode = vk::CullModeFlagBits::eBack;
        rasterInfo.frontFace = vk::FrontFace::eClockwise;
        rasterInfo.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthState{};
        depthState.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
        depthState.depthWriteEnable = hasDepth ? VK_TRUE : VK_FALSE;
        depthState.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthState.depthBoundsTestEnable = VK_FALSE;
        depthState.stencilTestEnable = VK_FALSE;

        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size());
        for (auto& attachment : colorBlendAttachments)
        {
            attachment.colorWriteMask =
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            attachment.blendEnable = VK_FALSE;
        }

        vk::PipelineColorBlendStateCreateInfo colorBlendInfo{};
        colorBlendInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlendInfo.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();

        std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = { *m_globalSetLayout, *shader.MaterialSetLayout };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        VulkanGraphicsPipeline pipeline{};

        try
        {
            pipeline.Layout = m_device.GetDevice().createPipelineLayout(layoutInfo);

            vk::PipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.viewMask = 0;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
            renderingInfo.pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data();
            renderingInfo.depthAttachmentFormat = depthFormat;
            renderingInfo.stencilAttachmentFormat = utils::HasStencilComponent(depthFormat) ? depthFormat : vk::Format::eUndefined;

            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.pNext = &renderingInfo;
            pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterInfo;
            pipelineInfo.pMultisampleState = &multisampleState;
            pipelineInfo.pDepthStencilState = &depthState;
            pipelineInfo.pColorBlendState = &colorBlendInfo;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = *pipeline.Layout;
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

        auto property = shader.Properties.find(std::string(name));

        if (property == shader.Properties.end())
            return;

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

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            commandBuffer.pipelineBarrier2(dependency);
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

            if (isSupported(vk::PresentModeKHR::eMailbox))
                presentMode = vk::PresentModeKHR::eMailbox;
            else if (isSupported(vk::PresentModeKHR::eImmediate))
                presentMode = vk::PresentModeKHR::eImmediate;

            const vk::Extent2D extent = capabilities.currentExtent;

            if (extent.width == 0 || extent.height == 0 || extent.width == UINT32_MAX)
                return false;

            uint32_t imageCount = std::max(capabilities.minImageCount + 1, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT));

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

    void VulkanResourceManager::ReflectShader(VulkanShader& shader, std::span<const uint32_t> spirvCode)
    {
        SpvReflectShaderModule reflModule;
        if (spvReflectCreateShaderModule(spirvCode.size() * sizeof(uint32_t), spirvCode.data(), &reflModule) != SPV_REFLECT_RESULT_SUCCESS)
            return;

        uint32_t count = 0;
        spvReflectEnumerateDescriptorSets(&reflModule, &count, nullptr);
        std::vector<SpvReflectDescriptorSet*> sets(count);
        spvReflectEnumerateDescriptorSets(&reflModule, &count, sets.data());

        for (auto* set : sets)
        {
            if (set->set != 1) continue;

            for (uint32_t i = 0; i < set->binding_count; ++i)
            {
                const SpvReflectDescriptorBinding* binding = set->bindings[i];

                if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                {
                    if (binding->binding < kMaterialBindingStart)
                        LOG_WARN("[Vulkan] MaterialBlock binding ({}) collides with globals (>= {} needed)!", binding->binding, kMaterialBindingStart);

                    shader.MaterialUniformSize = binding->block.size;
                    shader.MaterialUboBinding  = binding->binding;

                    for (uint32_t m = 0; m < binding->block.member_count; ++m)
                    {
                        const SpvReflectBlockVariable& member = binding->block.members[m];
                        shader.Properties[member.name] = ShaderPropertyInfo{ .Offset = member.offset, .Size = member.size };
                    }
                }
                else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                {
                    if (binding->binding < kMaterialBindingStart)
                        LOG_WARN("[Vulkan] Material samplers '{}' binding ({}) collides with globals!", binding->name, binding->binding);

                    shader.TextureBindings[binding->name] = ShaderTextureBinding{ binding->binding };
                }
            }
        }

        spvReflectDestroyShaderModule(&reflModule);
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
