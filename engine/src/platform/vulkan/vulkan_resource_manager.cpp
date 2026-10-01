#include "vulkan_resource_manager.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
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
        DestroyResources();

        if (m_defaultSampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device.GetDevice(), m_defaultSampler, nullptr);

        if (m_descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_device.GetDevice(), m_descriptorPool, nullptr);

        if (m_globalSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_device.GetDevice(), m_globalSetLayout, nullptr);
    }

    void VulkanResourceManager::CreateDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 3> poolSizes{};

        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        poolSizes[0].descriptorCount = 2;

        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = 100;

        poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[2].descriptorCount = 100;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 100;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        if (vkCreateDescriptorPool(
                m_device.GetDevice(),
                &poolInfo,
                nullptr,
                &m_descriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }

    void VulkanResourceManager::CreateGlobalSetLayout()
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        bindings[0].binding = kCameraBlockBinding;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        bindings[1].binding = kLightingBlockBinding;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = kShadowMapBinding;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(
                m_device.GetDevice(),
                &createInfo,
                nullptr,
                &m_globalSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create global descriptor set layout");
        }
    }

    void VulkanResourceManager::CreateDefaultSampler()
    {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = VK_FILTER_LINEAR;
        createInfo.minFilter = VK_FILTER_LINEAR;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.anisotropyEnable = VK_FALSE;
        createInfo.maxAnisotropy = 1.0f;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.unnormalizedCoordinates = VK_FALSE;
        createInfo.compareEnable = VK_FALSE;
        createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        createInfo.minLod = 0.0f;
        createInfo.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(m_device.GetDevice(), &createInfo, nullptr, &m_defaultSampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create default sampler");
    }

    RenderTargetHandle VulkanResourceManager::GetCurrentBackBuffer() const
    {
        return m_primarySurface.SwapchainTargetHandles[m_primarySurface.CurrentImageIndex];
    }

    void VulkanResourceManager::RecreateSwapchain(VulkanSurface& surface)
    {
        m_device.WaitIdle();
        DestroySwapchainResources(surface);
        CreateSwapchainResources(surface);
    }

    VertexBufferHandle VulkanResourceManager::CreateVertexBuffer(const VertexBufferInfo& info, const void* data)
    {
        VulkanVertexBuffer buffer{};
        buffer.Size = info.Size;
        buffer.Layout = info.Layout;
        buffer.Usage = info.Usage;

        if (info.Usage == BufferUsage::Static && data)
        {
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

            if (!CreateBuffer(info.Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory))
                return {};

            void* mapped = nullptr;

            if (vkMapMemory(m_device.GetDevice(), stagingMemory, 0, info.Size, 0, &mapped) != VK_SUCCESS)
            {
                vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
                return {};
            }

            std::memcpy(mapped, data, static_cast<size_t>(info.Size));
            vkUnmapMemory(m_device.GetDevice(), stagingMemory);

            if (!CreateBuffer(info.Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer.Buffer, buffer.Memory))
            {
                vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
                return {};
            }

            CopyBuffer(stagingBuffer, buffer.Buffer, info.Size);

            vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
        }
        else
        {
            if (!CreateBuffer(info.Size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer.Buffer, buffer.Memory))
                return {};

            if (data)
            {
                void* mapped = nullptr;

                if (vkMapMemory(m_device.GetDevice(), buffer.Memory, 0, info.Size, 0, &mapped) != VK_SUCCESS)
                {
                    vkDestroyBuffer(m_device.GetDevice(), buffer.Buffer, nullptr);
                    vkFreeMemory(m_device.GetDevice(), buffer.Memory, nullptr);
                    return {};
                }

                std::memcpy(mapped, data, static_cast<size_t>(info.Size));
                vkUnmapMemory(m_device.GetDevice(), buffer.Memory);
            }
        }

        return m_vertexBuffers.Register(std::move(buffer));
    }

    void VulkanResourceManager::DestroyVertexBuffer(VertexBufferHandle handle)
    {
        VulkanVertexBuffer* buffer = m_vertexBuffers.Find(handle);
        if (!buffer)
            return;

        if (buffer->Buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(m_device.GetDevice(), buffer->Buffer, nullptr);

        if (buffer->Memory != VK_NULL_HANDLE)
            vkFreeMemory(m_device.GetDevice(), buffer->Memory, nullptr);

        m_vertexBuffers.Unregister(handle);
    }

    IndexBufferHandle VulkanResourceManager::CreateIndexBuffer(const IndexBufferInfo& info, const void* data)
    {
        VulkanIndexBuffer buffer{};
        buffer.Size = info.Size;
        buffer.Type = info.Type;
        buffer.Usage = info.Usage;

        if (info.Usage == BufferUsage::Static && data)
        {
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

            if (!CreateBuffer(info.Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory))
                return {};

            void* mapped = nullptr;

            if (vkMapMemory(m_device.GetDevice(), stagingMemory, 0, info.Size, 0, &mapped) != VK_SUCCESS)
            {
                vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
                return {};
            }

            std::memcpy(mapped, data, static_cast<size_t>(info.Size));
            vkUnmapMemory(m_device.GetDevice(), stagingMemory);

            if (!CreateBuffer(info.Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer.Buffer, buffer.Memory))
            {
                vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
                return {};
            }

            CopyBuffer(stagingBuffer, buffer.Buffer, info.Size);

            vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
        }
        else
        {
            if (!CreateBuffer(info.Size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer.Buffer, buffer.Memory))
                return {};

            if (data)
            {
                void* mapped = nullptr;

                if (vkMapMemory(m_device.GetDevice(), buffer.Memory, 0, info.Size, 0, &mapped) != VK_SUCCESS)
                {
                    vkDestroyBuffer(m_device.GetDevice(), buffer.Buffer, nullptr);
                    vkFreeMemory(m_device.GetDevice(), buffer.Memory, nullptr);
                    return {};
                }

                std::memcpy(mapped, data, static_cast<size_t>(info.Size));
                vkUnmapMemory(m_device.GetDevice(), buffer.Memory);
            }
        }

        return m_indexBuffers.Register(std::move(buffer));
    }

    void VulkanResourceManager::DestroyIndexBuffer(IndexBufferHandle handle)
    {
        VulkanIndexBuffer* buffer = m_indexBuffers.Find(handle);
        if (!buffer)
            return;

        if (buffer->Buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(m_device.GetDevice(), buffer->Buffer, nullptr);

        if (buffer->Memory != VK_NULL_HANDLE)
            vkFreeMemory(m_device.GetDevice(), buffer->Memory, nullptr);

        m_indexBuffers.Unregister(handle);
    }

    ShaderHandle VulkanResourceManager::CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary)
    {
        VulkanShader shader{};

        shader.Vertex = CreateShaderModule(vertexBinary);

        if (shader.Vertex == VK_NULL_HANDLE)
            return {};

        shader.Fragment = CreateShaderModule(fragmentBinary);

        if (shader.Fragment == VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(m_device.GetDevice(), shader.Vertex, nullptr);
            return {};
        }

        ReflectShader(shader, vertexBinary, VK_SHADER_STAGE_VERTEX_BIT);
        ReflectShader(shader, fragmentBinary, VK_SHADER_STAGE_FRAGMENT_BIT);

        std::vector<VkDescriptorSetLayoutBinding> bindings;

        if (shader.MaterialUniformSize > 0)
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = shader.MaterialUboBinding;
            binding.descriptorCount = 1;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(binding);
        }

        for (const auto& [name, texture] : shader.TextureBindings)
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = texture.Binding;
            binding.descriptorCount = 1;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

        if (vkCreateDescriptorSetLayout(
                m_device.GetDevice(),
                &layoutInfo,
                nullptr,
                &shader.MaterialSetLayout) != VK_SUCCESS)
        {
            vkDestroyShaderModule(m_device.GetDevice(), shader.Vertex, nullptr);
            vkDestroyShaderModule(m_device.GetDevice(), shader.Fragment, nullptr);
            return {};
        }

        return m_shaders.Register(std::move(shader));
    }

    void VulkanResourceManager::DestroyShader(ShaderHandle handle)
    {
        VulkanShader* shader = m_shaders.Find(handle);
        if (!shader)
            return;

        if (shader->MaterialSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_device.GetDevice(), shader->MaterialSetLayout, nullptr);

        if (shader->Vertex != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_device.GetDevice(), shader->Vertex, nullptr);

        if (shader->Fragment != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_device.GetDevice(), shader->Fragment, nullptr);

        m_shaders.Unregister(handle);
    }

    MaterialHandle VulkanResourceManager::CreateMaterial(ShaderHandle shaderHandle)
    {
        VulkanShader* shader = m_shaders.Find(shaderHandle);

        if (!shader)
            return {};

        VulkanMaterial material{};
        material.Shader = shaderHandle;
        material.UniformBufferSize = shader->MaterialUniformSize;

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &shader->MaterialSetLayout;

        if (vkAllocateDescriptorSets(m_device.GetDevice(), &allocateInfo, &material.DescriptorSet) != VK_SUCCESS)
            return {};

        if (material.UniformBufferSize > 0)
        {
            if (!CreateBuffer(material.UniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, material.UniformBuffer, material.UniformBufferMemory))
            {
                vkFreeDescriptorSets(m_device.GetDevice(), m_descriptorPool, 1, &material.DescriptorSet);
                return {};
            }

            if (vkMapMemory(m_device.GetDevice(), material.UniformBufferMemory, 0, material.UniformBufferSize, 0, &material.MappedData) != VK_SUCCESS)
            {
                vkDestroyBuffer(m_device.GetDevice(), material.UniformBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), material.UniformBufferMemory, nullptr);
                vkFreeDescriptorSets(m_device.GetDevice(), m_descriptorPool, 1, &material.DescriptorSet);
                return {};
            }

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = material.UniformBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = material.UniformBufferSize;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = material.DescriptorSet;
            write.dstBinding = shader->MaterialUboBinding;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_device.GetDevice(), 1, &write, 0, nullptr);
        }

        return m_materials.Register(std::move(material));
    }

    void VulkanResourceManager::DestroyMaterial(MaterialHandle handle)
    {
        VulkanMaterial* material = m_materials.Find(handle);
        if (!material)
            return;

        if (material->MappedData)
        {
            vkUnmapMemory(m_device.GetDevice(), material->UniformBufferMemory);
            material->MappedData = nullptr;
        }

        if (material->UniformBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(m_device.GetDevice(), material->UniformBuffer, nullptr);

        if (material->UniformBufferMemory != VK_NULL_HANDLE)
            vkFreeMemory(m_device.GetDevice(), material->UniformBufferMemory, nullptr);

        if (material->DescriptorSet != VK_NULL_HANDLE)
            vkFreeDescriptorSets(m_device.GetDevice(), m_descriptorPool, 1, &material->DescriptorSet);

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

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_defaultSampler;
        imageInfo.imageView = texture.View;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = material.DescriptorSet;
        write.dstBinding = it->second.Binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device.GetDevice(), 1, &write, 0, nullptr);
    }

    TextureHandle VulkanResourceManager::CreateTexture(const TextureInfo& info, const void* data)
    {
        const VkFormat format = utils::GetVkFormat(info.Format);

        if (format == VK_FORMAT_UNDEFINED)
            return TextureHandle::Invalid();

        VkImageUsageFlags usage = 0;

        if (HasTextureUsage(info.Usage, TextureUsage::Sampled))
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

        if (HasTextureUsage(info.Usage, TextureUsage::ColorAttachment))
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (HasTextureUsage(info.Usage, TextureUsage::DepthStencilAttachment))
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        if (usage == 0)
            return TextureHandle::Invalid();

        VulkanTexture texture{};
        texture.Format = format;
        texture.Width = info.Width;
        texture.Height = info.Height;
        texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        texture.Aspect = utils::GetImageAspect(info.Format);

        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {info.Width, info.Height, 1};
        imageInfo.mipLevels = info.MipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (data)
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        if (vkCreateImage(m_device.GetDevice(), &imageInfo, nullptr, &texture.Image) != VK_SUCCESS)
            return TextureHandle::Invalid();

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(m_device.GetDevice(), texture.Image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = m_device.FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device.GetDevice(), &allocateInfo, nullptr, &texture.Memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_device.GetDevice(), texture.Image, nullptr);
            return TextureHandle::Invalid();
        }

        vkBindImageMemory(m_device.GetDevice(), texture.Image, texture.Memory, 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = texture.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = texture.Aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = info.MipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device.GetDevice(), &viewInfo, nullptr, &texture.View) != VK_SUCCESS)
        {
            vkFreeMemory(m_device.GetDevice(), texture.Memory, nullptr);
            vkDestroyImage(m_device.GetDevice(), texture.Image, nullptr);
            return TextureHandle::Invalid();
        }

        TextureHandle handle = m_textures.Register(texture);

        if (data)
            UploadTextureData(m_textures.Get(handle), info, data);

        return handle;
    }

    void VulkanResourceManager::DestroyTexture(TextureHandle handle)
    {
        VulkanTexture* texture = m_textures.Find(handle);

        if (!texture)
            return;

        if (texture->View != VK_NULL_HANDLE)
            vkDestroyImageView(m_device.GetDevice(), texture->View, nullptr);

        if (!texture->IsSwapchainImage)
        {
            if (texture->Image != VK_NULL_HANDLE)
                vkDestroyImage(m_device.GetDevice(), texture->Image, nullptr);

            if (texture->Memory != VK_NULL_HANDLE)
                vkFreeMemory(m_device.GetDevice(), texture->Memory, nullptr);
        }

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

        if (shader.Vertex == VK_NULL_HANDLE || shader.Fragment == VK_NULL_HANDLE)
        {
            LOG_ERROR("[Renderer] Cannot create Vulkan pipeline without valid shader modules");
            return {};
        }

        const VulkanRenderTarget& target = GetRenderTarget(info.Target);

        std::vector<VkFormat> colorFormats;
        colorFormats.reserve(target.ColorAttachments.size());
        for (TextureHandle colorHandle : target.ColorAttachments)
        {
            colorFormats.push_back(GetTexture(colorHandle).Format);
        }

        const bool hasDepth = static_cast<bool>(target.DepthAttachment);
        const VkFormat depthFormat = hasDepth ? GetTexture(target.DepthAttachment).Format : VK_FORMAT_UNDEFINED;

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = shader.Vertex;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = shader.Fragment;
        shaderStages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = info.Layout.GetStride();
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::vector<VkVertexInputAttributeDescription> attributes;
        uint32_t location = 0;
        for (const auto& element : info.Layout)
        {
            VkVertexInputAttributeDescription attribute{};
            attribute.location = location++;
            attribute.binding = 0;
            switch (element.Type)
            {
                case ShaderDataType::Float:  attribute.format = VK_FORMAT_R32_SFLOAT; break;
                case ShaderDataType::Float2: attribute.format = VK_FORMAT_R32G32_SFLOAT; break;
                case ShaderDataType::Float3: attribute.format = VK_FORMAT_R32G32B32_SFLOAT; break;
                case ShaderDataType::Float4: attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
                case ShaderDataType::Int:    attribute.format = VK_FORMAT_R32_SINT; break;
                case ShaderDataType::Int2:   attribute.format = VK_FORMAT_R32G32_SINT; break;
                case ShaderDataType::Int3:   attribute.format = VK_FORMAT_R32G32B32_SINT; break;
                case ShaderDataType::Int4:   attribute.format = VK_FORMAT_R32G32B32A32_SINT; break;
            }
            attribute.offset = element.Offset;
            attributes.push_back(attribute);
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &binding;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterInfo{};
        rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterInfo.depthClampEnable = VK_FALSE;
        rasterInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterInfo.lineWidth = 1.0f;
        rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterInfo.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthState{};
        depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthState.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
        depthState.depthWriteEnable = hasDepth ? VK_TRUE : VK_FALSE;
        depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthState.depthBoundsTestEnable = VK_FALSE;
        depthState.stencilTestEnable = VK_FALSE;

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size());
        for (auto& attachment : colorBlendAttachments)
        {
            attachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            attachment.blendEnable = VK_FALSE;
        }

        VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlendInfo.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();

        VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkDescriptorSetLayout setLayouts[2] = { m_globalSetLayout, shader.MaterialSetLayout };

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = setLayouts;

        VulkanGraphicsPipeline pipeline{};

        if (vkCreatePipelineLayout(m_device.GetDevice(), &layoutInfo, nullptr, &pipeline.Layout) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create Vulkan pipeline layout");
            return {};
        }

        // --- ITT A LÉNYEGI JAVÍTÁS: a render target tényleges attachment-jeiből épül fel ---
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.empty() ? nullptr : colorFormats.data();
        renderingInfo.depthAttachmentFormat = depthFormat; // VK_FORMAT_UNDEFINED, ha nincs depth

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterInfo;
        pipelineInfo.pMultisampleState = &multisampleState;
        pipelineInfo.pDepthStencilState = &depthState;
        pipelineInfo.pColorBlendState = &colorBlendInfo;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipeline.Layout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(m_device.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.Pipeline) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create Vulkan graphics pipeline");
            vkDestroyPipelineLayout(m_device.GetDevice(), pipeline.Layout, nullptr);
            pipeline.Layout = VK_NULL_HANDLE;
            return pipeline;
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

        if (!material.MappedData)
            return;

        std::memcpy(static_cast<std::byte*>(material.MappedData) + property->second.Offset, data.data(), data.size());
    }

    bool VulkanResourceManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device.GetDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
            return false;

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(m_device.GetDevice(), buffer, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = m_device.FindMemoryType(requirements.memoryTypeBits, properties);

        if (vkAllocateMemory(m_device.GetDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_device.GetDevice(), buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        if (vkBindBufferMemory(m_device.GetDevice(), buffer, memory, 0) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_device.GetDevice(), buffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), memory, nullptr);
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    bool VulkanResourceManager::CreateImage(const VkImageCreateInfo& info, VulkanTexture& texture) const
    {
        if (vkCreateImage(m_device.GetDevice(), &info, nullptr, &texture.Image) != VK_SUCCESS)
            return false;

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(m_device.GetDevice(), texture.Image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = m_device.FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device.GetDevice(), &allocInfo, nullptr, &texture.Memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_device.GetDevice(), texture.Image, nullptr);
            texture.Image = VK_NULL_HANDLE;
            return false;
        }

        if (vkBindImageMemory(m_device.GetDevice(), texture.Image, texture.Memory, 0) != VK_SUCCESS)
        {
            vkDestroyImage(m_device.GetDevice(), texture.Image, nullptr);
            vkFreeMemory(m_device.GetDevice(), texture.Memory, nullptr);
            texture.Image = VK_NULL_HANDLE;
            texture.Memory = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    bool VulkanResourceManager::CreateImageView(VulkanTexture& texture, VkImageAspectFlags aspect) const
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = texture.Format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        return vkCreateImageView(m_device.GetDevice(), &viewInfo, nullptr, &texture.View) == VK_SUCCESS;
    }

    void VulkanResourceManager::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const
    {
        m_device.ImmediateSubmit([&](VkCommandBuffer commandBuffer)
        {
            VkBufferCopy copy{};
            copy.size = size;
            vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copy);
        });
    }

    void VulkanResourceManager::TransitionImageLayout(VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout) const
    {
        m_device.ImmediateSubmit([&](VkCommandBuffer commandBuffer)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = aspect;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = 0;
                destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            }
            else
            {
                assert(false && "Unsupported image layout transition");
                return;
            }

            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });
    }

    void VulkanResourceManager::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
    {
        m_device.ImmediateSubmit([&](VkCommandBuffer commandBuffer)
        {
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { width, height, 1 };

            vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        });
    }

    void VulkanResourceManager::UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const
    {
        const VkDeviceSize size = static_cast<VkDeviceSize>(info.Width) * info.Height * 4;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

        if (!CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory))
            return;

        void* mapped = nullptr;

        if (vkMapMemory(m_device.GetDevice(), stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
            return;
        }

        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(m_device.GetDevice(), stagingMemory);

        TransitionImageLayout(texture.Image, texture.Aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CopyBufferToImage(stagingBuffer, texture.Image, info.Width, info.Height);
        TransitionImageLayout(texture.Image, texture.Aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        texture.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
    }

    TextureHandle VulkanResourceManager::WrapSwapchainImage(VkImage image, VkFormat format, uint32_t width, uint32_t height)
    {
        VulkanTexture texture{};
        texture.Image = image;
        texture.Format = format;
        texture.Width = width;
        texture.Height = height;
        texture.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        texture.Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        texture.IsSwapchainImage = true;

        if (!CreateImageView(texture, texture.Aspect))
            return {};

        return m_textures.Register(std::move(texture));
    }

    RenderTargetHandle VulkanResourceManager::CreateSwapchainRenderTarget(uint32_t width, uint32_t height, VkFormat colorFormat, VkImage swapchainImage)
    {
        VulkanRenderTarget target{};
        target.Width = width;
        target.Height = height;
        target.IsSwapchain = true;

        TextureHandle texture = WrapSwapchainImage(
            swapchainImage,
            colorFormat,
            width,
            height);

        if (!texture)
            return {};

        VulkanTexture& vulkanTexture = m_textures.Get(texture);
        vulkanTexture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        target.ColorAttachments.push_back(texture);

        return m_renderTargets.Register(std::move(target));
    }

    bool VulkanResourceManager::CreateSwapchainResources(VulkanSurface& surface)
    {
        VkSurfaceCapabilitiesKHR capabilities{};

        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &capabilities) != VK_SUCCESS)
            return false;

        uint32_t formatCount = 0;

        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &formatCount, nullptr);

        if (formatCount == 0)
            return false;

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats[0];

        for (const VkSurfaceFormatKHR& format : formats)
        {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surfaceFormat = format;
                break;
            }
        }

        uint32_t presentModeCount = 0;

        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &presentModeCount, nullptr);

        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

        for (VkPresentModeKHR mode : presentModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = mode;
                break;
            }
        }

        VkExtent2D extent = capabilities.currentExtent;

        if (extent.width == 0 || extent.height == 0 || extent.width == UINT32_MAX)
            return false;

        uint32_t imageCount = capabilities.minImageCount + 1;

        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
            imageCount = capabilities.maxImageCount;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_device.GetSurface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(m_device.GetDevice(), &createInfo, nullptr, &surface.Swapchain) != VK_SUCCESS)
            return false;

        surface.Extent = extent;
        surface.Format = surfaceFormat.format;

        uint32_t imageCountActual = 0;

        vkGetSwapchainImagesKHR(m_device.GetDevice(), surface.Swapchain, &imageCountActual, nullptr);

        std::vector<VkImage> images(imageCountActual);
        vkGetSwapchainImagesKHR(m_device.GetDevice(), surface.Swapchain, &imageCountActual, images.data());

        surface.SwapchainTargetHandles.reserve(imageCountActual);

        for (VkImage image : images)
        {
            RenderTargetHandle target = CreateSwapchainRenderTarget(extent.width, extent.height, surface.Format, image);

            if (!target)
            {
                DestroySwapchainResources(surface);
                return false;
            }

            surface.SwapchainTargetHandles.push_back(target);
        }

        surface.ImagesInFlight.resize(imageCountActual, VK_NULL_HANDLE);
        surface.RenderFinishedSemaphores.resize(imageCountActual, VK_NULL_HANDLE);

        for (VkSemaphore& semaphore : surface.RenderFinishedSemaphores)
        {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            if (vkCreateSemaphore(m_device.GetDevice(), &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
            {
                DestroySwapchainResources(surface);
                return false;
            }
        }

        surface.CurrentImageIndex = 0;

        return true;
    }

    void VulkanResourceManager::DestroySwapchainResources(VulkanSurface& surface)
    {
        for (RenderTargetHandle handle : surface.SwapchainTargetHandles)
            DestroyRenderTarget(handle);

        surface.SwapchainTargetHandles.clear();

        for (VkSemaphore semaphore : surface.RenderFinishedSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(m_device.GetDevice(), semaphore, nullptr);
        }

        surface.RenderFinishedSemaphores.clear();
        surface.ImagesInFlight.clear();

        if (surface.Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_device.GetDevice(), surface.Swapchain, nullptr);
            surface.Swapchain = VK_NULL_HANDLE;
        }
    }

    VkShaderModule VulkanResourceManager::CreateShaderModule(std::span<const uint32_t> code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size_bytes();
        createInfo.pCode = code.data();

        VkShaderModule module = VK_NULL_HANDLE;

        if (vkCreateShaderModule(m_device.GetDevice(), &createInfo, nullptr, &module) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        return module;
    }

    void VulkanResourceManager::ReflectShader(VulkanShader& shader, std::span<const uint32_t> spirvCode, VkShaderStageFlagBits stage)
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
        for (auto& [info, pipeline] : m_graphicsPipelines)
        {
            if (pipeline.Pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(m_device.GetDevice(), pipeline.Pipeline, nullptr);
            if (pipeline.Layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(m_device.GetDevice(), pipeline.Layout, nullptr);
        }
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
