#include "vulkan_resource_manager.hpp"

#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "GLFW/glfw3.h"
#include <array>
#include <cstring>
#include <string>
#include <spirv_reflect.h>
#include "utils.hpp"

namespace crimson::vulkan
{
    VulkanResourceManager::VulkanResourceManager(VulkanDevice &device)
        : m_device(device)
    {
    }

    void VulkanResourceManager::Init()
    {
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1000;

        if (vkCreateDescriptorPool(m_device.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create descriptor pool!");
            m_descriptorPool = VK_NULL_HANDLE;
        }

        VkDescriptorSetLayoutBinding globalBindings[3]{};

        globalBindings[0].binding = kCameraBlockBinding;
        globalBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        globalBindings[0].descriptorCount = 1;
        globalBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        globalBindings[1].binding = kLightingBlockBinding;
        globalBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        globalBindings[1].descriptorCount = 1;
        globalBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        globalBindings[2].binding = kShadowMapBinding;
        globalBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        globalBindings[2].descriptorCount = 1;
        globalBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = globalBindings;

        if (vkCreateDescriptorSetLayout(m_device.GetDevice(), &layoutInfo, nullptr, &m_globalSetLayout) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create global descriptor set layout!");
            m_globalSetLayout = VK_NULL_HANDLE;
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;

        if (vkCreateSampler(m_device.GetDevice(), &samplerInfo, nullptr, &m_defaultSampler) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create default sampler!");
        }
    }

    void VulkanResourceManager::Clear()
    {
        for (const auto& entry : m_graphicsPipelines)
        {
            const VulkanGraphicsPipeline& pipeline = entry.second;

            if (pipeline.Pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_device.GetDevice(), pipeline.Pipeline, nullptr);
            }

            if (pipeline.Layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(m_device.GetDevice(), pipeline.Layout, nullptr);
            }

            if (pipeline.DescriptorSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_device.GetDevice(), pipeline.DescriptorSetLayout, nullptr);
            }
        }

        m_graphicsPipelines.clear();

        for (const auto& shader : m_shaders)
        {
            if (shader.Vertex != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device.GetDevice(), shader.Vertex, nullptr);
            }

            if (shader.Fragment != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device.GetDevice(), shader.Fragment, nullptr);
            }

            if (shader.MaterialSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_device.GetDevice(), shader.MaterialSetLayout, nullptr);
            }
        }

        m_shaders.Clear();

        for (const auto& material : m_materials)
        {
            if (material.DescriptorSet != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(m_device.GetDevice(), m_descriptorPool, 1, &material.DescriptorSet);
            }

            if (material.UniformBuffer != VK_NULL_HANDLE)
            {
                if (material.MappedData != nullptr)
                {
                    vkUnmapMemory(m_device.GetDevice(), material.UniformBufferMemory);
                }

                vkDestroyBuffer(m_device.GetDevice(), material.UniformBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), material.UniformBufferMemory, nullptr);
            }
        }

        m_materials.Clear();


        for (const auto& buffer : m_vertexBuffers)
        {
            if (buffer.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_device.GetDevice(), buffer.Buffer, nullptr);
            }

            if (buffer.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device.GetDevice(), buffer.Memory, nullptr);
            }
        }

        m_vertexBuffers.Clear();

        for (const auto& buffer : m_indexBuffers)
        {
            if (buffer.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_device.GetDevice(), buffer.Buffer, nullptr);
            }

            if (buffer.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device.GetDevice(), buffer.Memory, nullptr);
            }
        }

        m_indexBuffers.Clear();

        for (const VulkanRenderTarget& target : m_renderTargets)
        {
            for (auto& textureHandle : target.ColorAttachments)
            {
                DestroyTexture(textureHandle);
            }

            if (target.DepthAttachment)
            {
                DestroyTexture(*target.DepthAttachment);
            }
        }

        m_renderTargets.Clear();

        for (const VulkanSurface& surface : m_renderSurfaces)
        {
            for (VkSemaphore sem : surface.RenderFinishedSemaphores)
            {
                if (sem != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(m_device.GetDevice(), sem,nullptr);
                }
            }

            if (surface.Swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(m_device.GetDevice(), surface.Swapchain, nullptr);
            }

            if (surface.Surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(m_device.GetInstance(), surface.Surface, nullptr);
            }
        }

        m_renderSurfaces.Clear();

        if (m_descriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_device.GetDevice(), m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }

        if (m_globalSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_device.GetDevice(), m_globalSetLayout, nullptr);
            m_globalSetLayout = VK_NULL_HANDLE;
        }

        if (m_defaultSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device.GetDevice(), m_defaultSampler, nullptr);
            m_defaultSampler = VK_NULL_HANDLE;
        }
    }

    RenderSurfaceHandle VulkanResourceManager::CreateRenderSurface(const Window& window)
    {
        VulkanSurface surface;
        surface.Extent = { window.Width(), window.Height() };
        if (glfwCreateWindowSurface(m_device.GetInstance(), static_cast<GLFWwindow*>(window.GetNativeHandle()), nullptr, &surface.Surface) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Failed to create Vulkan surface");
            return RenderSurfaceHandle::Invalid();
        }

        if (!CreateSwapchainResources(surface))
        {
            if (surface.Surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(m_device.GetInstance(), surface.Surface, nullptr);
            }

            return RenderSurfaceHandle::Invalid();
        }

        return m_renderSurfaces.Register(std::move(surface));
    }

    RenderTargetHandle VulkanResourceManager::GetCurrentBackBuffer(RenderSurfaceHandle renderSurface) const
    {
        const VulkanSurface& surface = m_renderSurfaces.Get(renderSurface);
        return surface.SwapchainTargetHandles[surface.CurrentImageIndex];
    }

    void VulkanResourceManager::RecreateSwapchain(RenderSurfaceHandle handle)
    {
        vkDeviceWaitIdle(m_device.GetDevice());
        VulkanSurface& surface = GetRenderSurface(handle);
        DestroySwapchainResources(surface);
        if (!CreateSwapchainResources(surface))
        {
            LOG_ERROR("[Resource Manager] Failed to recreate Vulkan swapchain");
        }
    }

    RenderTargetHandle VulkanResourceManager::CreateRenderTarget(const RenderTargetInfo& info)
    {
        VulkanRenderTarget target{};
        target.Width = info.Width;
        target.Height = info.Height;
        target.IsSwapchain = false;

        for (TextureFormat colorFormat : info.ColorFormats)
        {
            const TextureInfo colorInfo {
                .Width = info.Width, .Height = info.Height,
                .Format = colorFormat,
                .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled,
                .MipLevels = 1,
            };

            TextureHandle handle = CreateTexture(colorInfo, nullptr);
            if (!handle)
            {
                for (auto h : target.ColorAttachments) DestroyTexture(h);
                return RenderTargetHandle::Invalid();
            }

            target.ColorAttachments.push_back(handle);
        }

        if (info.DepthFormat)
        {
            const TextureInfo depthInfo {
                .Width = info.Width, .Height = info.Height,
                .Format = *info.DepthFormat,
                .Usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled, // Sampled temp?
                .MipLevels = 1,
            };

            target.DepthAttachment = CreateTexture(depthInfo, nullptr);
        }

        return m_renderTargets.Register(std::move(target));
    }

    void VulkanResourceManager::DestroyRenderTarget(RenderTargetHandle handle)
    {
        if (!handle)
            return;

        VulkanRenderTarget& target = m_renderTargets.Get(handle);

        for (TextureHandle h : target.ColorAttachments)
            DestroyTexture(h);

        if (target.DepthAttachment)
            DestroyTexture(*target.DepthAttachment);

        m_renderTargets.Unregister(handle);
    }

    TextureHandle VulkanResourceManager::GetColorAttachment(RenderTargetHandle handle, uint32_t index) const
    {
        return m_renderTargets.Get(handle).ColorAttachments.at(index);
    }

    std::optional<TextureHandle> VulkanResourceManager::GetDepthAttachment(RenderTargetHandle handle) const
    {
        return m_renderTargets.Get(handle).DepthAttachment;
    }

    VertexBufferHandle VulkanResourceManager::CreateVertexBuffer(const VertexBufferInfo& info, const void* data)
    {
        VulkanVertexBuffer buffer{};
        buffer.Size = info.Size;
        buffer.Layout = info.Layout;
        buffer.Usage = info.Usage;

        bool useStaging = info.Usage == BufferUsage::Static && data != nullptr;

        if (useStaging)
        {
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

            CreateBuffer(
                info.Size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer,
                stagingMemory
            );

            void* mapped = nullptr;
            vkMapMemory(m_device.GetDevice(), stagingMemory, 0, info.Size, 0, &mapped);
            std::memcpy(mapped, data, info.Size);
            vkUnmapMemory(m_device.GetDevice(), stagingMemory);

            CreateBuffer(
                info.Size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                buffer.Buffer,
                buffer.Memory
            );

            CopyBuffer(stagingBuffer, buffer.Buffer, info.Size);

            vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
        }
        else
        {
            constexpr VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            CreateBuffer(info.Size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, memoryFlags, buffer.Buffer, buffer.Memory);

            if (data != nullptr)
            {
                void* mapped = nullptr;
                vkMapMemory(m_device.GetDevice(), buffer.Memory, 0, info.Size, 0, &mapped);
                std::memcpy(mapped, data, info.Size);
                vkUnmapMemory(m_device.GetDevice(), buffer.Memory);
            }
        }

        return m_vertexBuffers.Register(buffer);
    }

    IndexBufferHandle VulkanResourceManager::CreateIndexBuffer(const IndexBufferInfo& info, const void* data)
    {
        VulkanIndexBuffer buffer{};
        buffer.Size = info.Size;
        buffer.Type = info.Type;
        buffer.Usage = info.Usage;

        const bool useStaging = info.Usage == BufferUsage::Static && data != nullptr;

        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (useStaging)
        {
            usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        if (useStaging)
        {
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

            CreateBuffer(
                info.Size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer,
                stagingMemory
            );

            void* mapped = nullptr;
            vkMapMemory(m_device.GetDevice(), stagingMemory, 0, info.Size, 0, &mapped);
            std::memcpy(mapped, data, info.Size);
            vkUnmapMemory(m_device.GetDevice(), stagingMemory);

            CreateBuffer(info.Size, usageFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer.Buffer, buffer.Memory);
            CopyBuffer(stagingBuffer, buffer.Buffer, info.Size);

            vkDestroyBuffer(m_device.GetDevice(), stagingBuffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
        }
        else
        {
            CreateBuffer(
                info.Size,
                usageFlags,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buffer.Buffer,
                buffer.Memory
            );

            if (data != nullptr)
            {
                void* mapped = nullptr;
                vkMapMemory(m_device.GetDevice(), buffer.Memory, 0, info.Size, 0, &mapped);
                std::memcpy(mapped, data, info.Size);
                vkUnmapMemory(m_device.GetDevice(), buffer.Memory);
            }
        }

        return m_indexBuffers.Register(buffer);
    }

    void VulkanResourceManager::DestroyVertexBuffer(VertexBufferHandle handle)
    {
        const VulkanVertexBuffer& buffer = m_vertexBuffers.Get(handle);

        if (buffer.Buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device.GetDevice(), buffer.Buffer, nullptr);
        }

        if (buffer.Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device.GetDevice(), buffer.Memory, nullptr);
        }

        m_vertexBuffers.Unregister(handle);
    }

    void VulkanResourceManager::DestroyIndexBuffer(IndexBufferHandle handle)
    {
        const VulkanIndexBuffer& buffer = m_indexBuffers.Get(handle);

        if (buffer.Buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device.GetDevice(), buffer.Buffer, nullptr);
        }

        if (buffer.Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device.GetDevice(), buffer.Memory, nullptr);
        }

        m_indexBuffers.Unregister(handle);
    }

    VkShaderModule VulkanResourceManager::CreateShaderModule(std::span<const uint32_t> code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        VkShaderModule module;
        if (vkCreateShaderModule(m_device.GetDevice(), &createInfo, nullptr, &module) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create shader module!");
            return VK_NULL_HANDLE;
        }

        return module;
    }

    ShaderHandle VulkanResourceManager::CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary)
    {
        VkShaderModule vertexModule = CreateShaderModule(vertexBinary);
        VkShaderModule fragmentModule = CreateShaderModule(fragmentBinary);

        if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
        {
            if (vertexModule != VK_NULL_HANDLE)   vkDestroyShaderModule(m_device.GetDevice(), vertexModule, nullptr);
            if (fragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(m_device.GetDevice(), fragmentModule, nullptr);
            return ShaderHandle::Invalid();
        }

        VulkanShader shader{};
        shader.Vertex = vertexModule;
        shader.Fragment = fragmentModule;

        ReflectShader(shader, vertexBinary, VK_SHADER_STAGE_VERTEX_BIT);
        ReflectShader(shader, fragmentBinary, VK_SHADER_STAGE_FRAGMENT_BIT);

        std::vector<VkDescriptorSetLayoutBinding> materialBindings;

        if (shader.MaterialUniformSize > 0)
        {
            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding = shader.MaterialUboBinding;
            uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount = 1;
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            materialBindings.push_back(uboBinding);
        }

        for (const auto& [name, texBinding] : shader.TextureBindings)
        {
            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = texBinding.Binding;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBinding.descriptorCount = 1;
            samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            materialBindings.push_back(samplerBinding);
        }

        VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
        materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        materialLayoutInfo.bindingCount = static_cast<uint32_t>(materialBindings.size());
        materialLayoutInfo.pBindings = materialBindings.empty() ? nullptr : materialBindings.data();

        if (vkCreateDescriptorSetLayout(m_device.GetDevice(), &materialLayoutInfo, nullptr, &shader.MaterialSetLayout) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create Material Descriptor Set Layout");
            vkDestroyShaderModule(m_device.GetDevice(), vertexModule, nullptr);
            vkDestroyShaderModule(m_device.GetDevice(), fragmentModule, nullptr);
            return ShaderHandle::Invalid();
        }

        return m_shaders.Register(shader);
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

    void VulkanResourceManager::DestroyShader(ShaderHandle handle)
    {
        if (!handle)
            return;

        auto& shader = m_shaders.Get(handle);
        VkDevice device = m_device.GetDevice();

        if (shader.Vertex)
        {
            vkDestroyShaderModule(device, shader.Vertex, nullptr);
        }

        if (shader.Fragment)
        {
            vkDestroyShaderModule(device, shader.Fragment, nullptr);
        }

        if (shader.MaterialSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, shader.MaterialSetLayout, nullptr);
        }

        m_shaders.Unregister(handle);
    }

    MaterialHandle VulkanResourceManager::CreateMaterial(ShaderHandle shaderHandle)
    {
        VulkanShader& shader = GetShader(shaderHandle);

        if (shader.MaterialSetLayout == VK_NULL_HANDLE)
        {
            LOG_ERROR("[Vulkan] Shader has no material descriptor set layout");
            return MaterialHandle::Invalid();
        }

        VulkanMaterial material{};
        material.Shader = shaderHandle;
        material.UniformBufferSize = static_cast<VkDeviceSize>(shader.MaterialUniformSize);

        if (shader.MaterialUniformSize > 0)
        {
            CreateBuffer(
                material.UniformBufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                material.UniformBuffer,
                material.UniformBufferMemory
            );

            if (vkMapMemory(m_device.GetDevice(), material.UniformBufferMemory, 0, material.UniformBufferSize, 0, &material.MappedData) != VK_SUCCESS)
            {
                LOG_ERROR("[Vulkan] Failed to map material uniform buffer");
                vkDestroyBuffer(m_device.GetDevice(), material.UniformBuffer, nullptr);
                vkFreeMemory(m_device.GetDevice(), material.UniformBufferMemory, nullptr);
                return MaterialHandle::Invalid();
            }
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shader.MaterialSetLayout;

        if (vkAllocateDescriptorSets(m_device.GetDevice(), &allocInfo, &material.DescriptorSet) != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to allocate material descriptor set!");
            if (material.MappedData) vkUnmapMemory(m_device.GetDevice(), material.UniformBufferMemory);
            if (material.UniformBuffer) vkDestroyBuffer(m_device.GetDevice(), material.UniformBuffer, nullptr);
            if (material.UniformBufferMemory) vkFreeMemory(m_device.GetDevice(), material.UniformBufferMemory, nullptr);
            return MaterialHandle::Invalid();
        }

        if (shader.MaterialUniformSize > 0)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = material.UniformBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = material.UniformBufferSize;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = material.DescriptorSet;
            write.dstBinding = shader.MaterialUboBinding; // reflektált, nem hardkódolt
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_device.GetDevice(), 1, &write, 0, nullptr);
        }

        return m_materials.Register(material);
    }

    void VulkanResourceManager::DestroyMaterial(MaterialHandle handle)
    {
        if (!handle)
        {
            return;
        }

        VulkanMaterial& material = m_materials.Get(handle);

        if (material.DescriptorSet != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(m_device.GetDevice(), m_descriptorPool, 1, &material.DescriptorSet);
            material.DescriptorSet = VK_NULL_HANDLE;
        }

        if (material.UniformBuffer != VK_NULL_HANDLE)
        {
            if (material.MappedData != nullptr)
            {
                vkUnmapMemory(m_device.GetDevice(), material.UniformBufferMemory);
            }

            vkDestroyBuffer(m_device.GetDevice(), material.UniformBuffer, nullptr);
            vkFreeMemory(m_device.GetDevice(), material.UniformBufferMemory, nullptr);

            material.UniformBuffer = VK_NULL_HANDLE;
            material.UniformBufferMemory = VK_NULL_HANDLE;
            material.MappedData = nullptr;
        }

        m_materials.Unregister(handle);
    }

    void VulkanResourceManager::SetMaterialTexture(MaterialHandle handle, std::string_view name, TextureHandle texture)
    {
        if (!handle)
        {
            LOG_WARN("[Vulkan] Invalid material handle in SetMaterialTexture");
            return;
        }

        VulkanMaterial& material = m_materials.Get(handle);
        if (material.Shader == ShaderHandle::Invalid())
        {
            LOG_WARN("[Vulkan] Material has no shader");
            return;
        }

        const VulkanShader& shader = m_shaders.Get(material.Shader);
        auto it = shader.TextureBindings.find(std::string(name));
        if (it == shader.TextureBindings.end())
        {
            LOG_WARN("[Vulkan] Texture property '{}' not found in shader", name);
            return;
        }

        if (!texture)
        {
            LOG_WARN("[Vulkan] Invalid texture handle passed for '{}'", name);
            return;
        }

        const VulkanTexture& tex = m_textures.Get(texture);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_defaultSampler;
        imageInfo.imageView = tex.View;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = material.DescriptorSet;
        write.dstBinding = it->second.Binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device.GetDevice(), 1, &write, 0, nullptr);
    }

    void VulkanResourceManager::SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data)
    {
        if (!handle)
        {
            LOG_WARN("[Vulkan] Invalid material handle in SetMaterialPropertyByNameImpl");
            return;
        }

        VulkanMaterial& material = m_materials.Get(handle);
        if (material.Shader == ShaderHandle::Invalid())
        {
            LOG_WARN("[Vulkan] Material has no shader");
            return;
        }

        const VulkanShader& shader = m_shaders.Get(material.Shader);
        const auto it = shader.Properties.find(std::string(name));
        if (it == shader.Properties.end())
        {
            LOG_WARN("[Vulkan] Material property '{}' not found", name);
            return;
        }

        const ShaderPropertyInfo& prop = it->second;
        if (data.size() > prop.Size || prop.Offset + data.size() > material.UniformBufferSize)
        {
            LOG_WARN("[Vulkan] Material property '{}' write exceeds uniform buffer bounds", name);
            return;
        }

        if (material.MappedData == nullptr)
        {
            LOG_WARN("[Vulkan] Material uniform buffer is not mapped");
            return;
        }

        std::memcpy(static_cast<std::byte*>(material.MappedData) + prop.Offset, data.data(), data.size());
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

        const bool hasDepth = target.DepthAttachment.has_value();
        const VkFormat depthFormat = hasDepth ? GetTexture(*target.DepthAttachment).Format : VK_FORMAT_UNDEFINED;

        // --- shader stages, vertex input, input assembly, viewport state ---
        // (ezek változatlanok a korábbi kódhoz képest)

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

        // Depth teszt csak akkor, ha tényleg van depth attachment (pl. egy pusztán szín render targetnél nincs)
        VkPipelineDepthStencilStateCreateInfo depthState{};
        depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthState.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
        depthState.depthWriteEnable = hasDepth ? VK_TRUE : VK_FALSE;
        depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthState.depthBoundsTestEnable = VK_FALSE;
        depthState.stencilTestEnable = VK_FALSE;

        // Színcsatorna blend állapot - annyi, ahány color attachment ténylegesen van (lehet 0!)
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

    void VulkanResourceManager::DestroySwapchainResources(VulkanSurface& surface)
    {
        for (RenderTargetHandle handle : surface.SwapchainTargetHandles)
        {
            DestroyRenderTarget(handle);
        }

        VkDevice device = m_device.GetDevice();

        for (VkSemaphore sem : surface.RenderFinishedSemaphores)
        {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(device, sem, nullptr);
        }

        if (surface.Swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device, surface.Swapchain, nullptr);

        surface.SwapchainTargetHandles.clear();
        surface.ImagesInFlight.clear();
        surface.RenderFinishedSemaphores.clear();
        surface.Swapchain = VK_NULL_HANDLE;
    }

    bool VulkanResourceManager::CreateSwapchainResources(VulkanSurface& surface)
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.GetPhysicalDevice(), surface.Surface, &capabilities);

        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            extent = capabilities.currentExtent;
        }
        else
        {
            extent = surface.Extent;
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface.Surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(m_device.GetDevice(), &createInfo, nullptr, &swapchain) != VK_SUCCESS)
        {
            LOG_ERROR("Swapchain create failed");
            return false;
        }

        surface.Extent = extent;
        surface.Swapchain = swapchain;
        surface.Format = createInfo.imageFormat;

        uint32_t count = 0;

        if (vkGetSwapchainImagesKHR(m_device.GetDevice(), swapchain, &count, nullptr) != VK_SUCCESS || count == 0)
        {
            LOG_ERROR("Failed to query swapchain images");
            vkDestroySwapchainKHR(m_device.GetDevice(), swapchain, nullptr);
            surface.Swapchain = VK_NULL_HANDLE;
            return false;
        }

        std::vector<VkImage> images(count);

        if (vkGetSwapchainImagesKHR(m_device.GetDevice(), swapchain, &count, images.data()) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to fetch swapchain images");
            vkDestroySwapchainKHR(m_device.GetDevice(), swapchain, nullptr);
            surface.Swapchain = VK_NULL_HANDLE;
            return false;
        }

        surface.SwapchainTargetHandles.resize(count);
        surface.ImagesInFlight.resize(count, VK_NULL_HANDLE);
        surface.RenderFinishedSemaphores.resize(count);

        for (uint32_t i = 0; i < count; i++)
        {
            surface.SwapchainTargetHandles[i] = CreateSwapchainRenderTarget(
                extent.width,
                extent.height,
                surface.Format,
                images[i]
            );

            if (!surface.SwapchainTargetHandles[i])
            {
                LOG_ERROR("Failed to wrap swapchain image {} as render target", i);
                return false;
            }

            VkSemaphoreCreateInfo sem{};
            sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            if (vkCreateSemaphore(m_device.GetDevice(), &sem, nullptr, &surface.RenderFinishedSemaphores[i]) != VK_SUCCESS)
            {
                LOG_ERROR("Render finished semaphore failed");
            }
        }

        return true;
    }

    void VulkanResourceManager::CreateImage(const VkImageCreateInfo& info, VulkanTexture& texture) const
    {
        if (vkCreateImage(m_device.GetDevice(), &info, nullptr, &texture.Image) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Create image failed");
            return;
        }

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(m_device.GetDevice(), texture.Image, &memReq);

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = memReq.size;
        alloc.memoryTypeIndex = m_device.FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device.GetDevice(), &alloc, nullptr, &texture.Memory) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Allocate image memory failed");
            return;
        }

        vkBindImageMemory(m_device.GetDevice(), texture.Image, texture.Memory, 0);
        texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        texture.Format = info.format;
        texture.Width  = info.extent.width;
        texture.Height = info.extent.height;
    }

    void VulkanResourceManager::CreateImageView(VulkanTexture& texture, VkImageAspectFlags aspect) const
    {
        texture.Aspect = aspect;

        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = texture.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = texture.Format;
        view.subresourceRange.aspectMask = aspect;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device.GetDevice(), &view, nullptr, &texture.View) != VK_SUCCESS)
        {
            LOG_ERROR("Create image view failed");
        }
    }

    void VulkanResourceManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const
    {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device.GetDevice(), &info, nullptr, &buffer) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Create buffer failed");
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(m_device.GetDevice(), buffer, &memReq);

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = memReq.size;
        alloc.memoryTypeIndex = m_device.FindMemoryType(memReq.memoryTypeBits, properties);

        if (vkAllocateMemory(m_device.GetDevice(), &alloc, nullptr, &memory) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Allocate buffer memory failed");
        }

        vkBindBufferMemory(m_device.GetDevice(), buffer, memory, 0);
    }

    void VulkanResourceManager::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamilyIdx();

        VkCommandPool commandPool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(m_device.GetDevice(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Create transfer command pool failed");
            return;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_device.GetDevice(), &allocInfo, &cmd) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Allocate transfer command buffer failed");
            vkDestroyCommandPool(m_device.GetDevice(), commandPool, nullptr);
            return;
        }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmd, &begin);

        VkBufferCopy copy{};
        copy.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &copy);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_device.GetGraphicsQueue());

        vkDestroyCommandPool(m_device.GetDevice(), commandPool, nullptr);
    }

    TextureHandle VulkanResourceManager::CreateTexture(const TextureInfo& info, const void* data)
    {
        VulkanTexture texture{};
        VkFormat format = utils::GetVkFormat(info.Format);

        VkImageUsageFlags usage = 0;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        if (utils::HasUsage(info.Usage, TextureUsage::Sampled))
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

        if (utils::HasUsage(info.Usage, TextureUsage::ColorAttachment))
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (utils::HasUsage(info.Usage, TextureUsage::DepthStencilAttachment))
        {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            aspect = utils::HasStencilComponent(format)
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                : VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        if (data != nullptr)
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { info.Width, info.Height, 1 };
        imageInfo.mipLevels = info.MipLevels > 0 ? info.MipLevels : 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CreateImage(imageInfo, texture);

        if (texture.Image == VK_NULL_HANDLE)
        {
            return TextureHandle::Invalid();
        }

        CreateImageView(texture, aspect);

        if (data != nullptr)
        {
            UploadTextureData(texture, info, data);
        }
        else if (aspect == VK_IMAGE_ASPECT_COLOR_BIT && utils::HasUsage(info.Usage, TextureUsage::Sampled))
        {
            // Sampler-ként használt, de üresen létrehozott textúrát is olvasható layout-ba tesszük
            TransitionImageLayout(texture.Image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            texture.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        return m_textures.Register(texture);
    }

    void VulkanResourceManager::DestroyTexture(TextureHandle handle)
    {
        if (!handle)
            return;

        VulkanTexture& texture = m_textures.Get(handle);
        VkDevice device = m_device.GetDevice();

        if (texture.View != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, texture.View, nullptr);
        }

        // Swapchain image-et nem mi allokáltuk, nem is mi szabadítjuk fel
        if (!texture.IsSwapchainImage)
        {
            if (texture.Image != VK_NULL_HANDLE)
                vkDestroyImage(device, texture.Image, nullptr);

            if (texture.Memory != VK_NULL_HANDLE)
                vkFreeMemory(device, texture.Memory, nullptr);
        }

        m_textures.Unregister(handle);
    }

    void VulkanResourceManager::TransitionImageLayout(VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout) const
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamilyIdx();

        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_device.GetDevice(), &poolInfo, nullptr, &pool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_device.GetDevice(), &allocInfo, &cmd);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { aspect, 0, 1, 0, 1 };

        VkPipelineStageFlags srcStage, dstStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else // pl. UNDEFINED -> SHADER_READ_ONLY (üres, csak allokált textúránál)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_device.GetGraphicsQueue());

        vkDestroyCommandPool(m_device.GetDevice(), pool, nullptr);
    }

    void VulkanResourceManager::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device.GetGraphicsQueueFamilyIdx();

        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_device.GetDevice(), &poolInfo, nullptr, &pool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_device.GetDevice(), &allocInfo, &cmd);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { width, height, 1 };

        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_device.GetGraphicsQueue());

        vkDestroyCommandPool(m_device.GetDevice(), pool, nullptr);
    }

    void VulkanResourceManager::UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const
    {
        const VkDeviceSize size = static_cast<VkDeviceSize>(info.Width) * info.Height * utils::GetBytesPerPixel(info.Format);

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

        CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMemory);

        void* mapped = nullptr;
        vkMapMemory(m_device.GetDevice(), stagingMemory, 0, size, 0, &mapped);
        std::memcpy(mapped, data, size);
        vkUnmapMemory(m_device.GetDevice(), stagingMemory);

        TransitionImageLayout(texture.Image, texture.Aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CopyBufferToImage(staging, texture.Image, info.Width, info.Height);
        TransitionImageLayout(texture.Image, texture.Aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        texture.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        vkDestroyBuffer(m_device.GetDevice(), staging, nullptr);
        vkFreeMemory(m_device.GetDevice(), stagingMemory, nullptr);
    }

    TextureHandle VulkanResourceManager::WrapSwapchainImage(VkImage image, VkFormat format, uint32_t width, uint32_t height)
    {
        VulkanTexture texture{};
        texture.Image = image;
        texture.Format = format;
        texture.Width = width;
        texture.Height = height;
        texture.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        texture.IsSwapchainImage = true;

        CreateImageView(texture, VK_IMAGE_ASPECT_COLOR_BIT);

        return m_textures.Register(texture);
    }

    RenderTargetHandle VulkanResourceManager::CreateSwapchainRenderTarget(uint32_t width, uint32_t height, VkFormat colorFormat, VkImage swapchainImage)
    {
        VulkanRenderTarget target{};
        target.Width = width;
        target.Height = height;
        target.IsSwapchain = true;

        target.ColorAttachments.push_back(WrapSwapchainImage(swapchainImage, colorFormat, width, height));

        const TextureInfo depthInfo {
            .Width = width, .Height = height,
            .Format = TextureFormat::Depth32F,
            .Usage = TextureUsage::DepthStencilAttachment,
            .MipLevels = 1,
        };
        target.DepthAttachment = CreateTexture(depthInfo, nullptr);

        return m_renderTargets.Register(std::move(target));
    }
}
