#include "vulkan_resource_manager.hpp"

#include "vulkan_renderer.hpp"
#include "crimson/core/log.hpp"
#include "GLFW/glfw3.h"
#include <cstring>
#include <fstream> // temp

namespace crimson::vulkan
{
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
        }

        m_shaders.Clear();

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
            for (auto& image : target.Colors)
            {
                vkDestroyImageView(m_device.GetDevice(), image.View, nullptr);

                if (image.Memory != VK_NULL_HANDLE)
                {
                    vkDestroyImage(m_device.GetDevice(), image.Image, nullptr);
                    vkFreeMemory(m_device.GetDevice(), image.Memory, nullptr);
                }
            }

            if (target.Depth)
            {
                vkDestroyImageView(m_device.GetDevice(), target.Depth->View, nullptr);

                if (target.Depth->Memory != VK_NULL_HANDLE)
                {
                    vkDestroyImage(m_device.GetDevice(), target.Depth->Image, nullptr);
                    vkFreeMemory(m_device.GetDevice(),target.Depth->Memory, nullptr);
                }
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
    }

    RenderSurfaceHandle VulkanResourceManager::CreateRenderSurface(const Window& window)
    {
        VulkanSurface surface;
        surface.Extent = { window.Width(), window.Height() };
        glfwCreateWindowSurface(m_device.GetInstance(), static_cast<GLFWwindow*>(window.GetNativeHandle()), nullptr, &surface.Surface);
        CreateSwapchainResources(surface);

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
        CreateSwapchainResources(surface);
    }

    RenderTargetHandle VulkanResourceManager::CreateRenderTarget(const RenderTargetDesc &desc, bool isSwapchain, std::span<const VkImage> swapchainImages)
    {
        VulkanRenderTarget target{};

        target.Width = desc.Width;
        target.Height = desc.Height;
        target.IsSwapchain = isSwapchain;

        for (uint32_t i = 0; i < desc.ColorCount; i++)
        {
            VulkanImage color{};

            if (isSwapchain)
            {
                color.Image = swapchainImages[i];
                color.Format = desc.ColorFormat;
            }
            else
            {
                color.Format = desc.ColorFormat;

                VkImageCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                info.imageType = VK_IMAGE_TYPE_2D;
                info.extent = {.width = desc.Width, .height = desc.Height, .depth = 1};
                info.mipLevels = 1;
                info.arrayLayers = 1;
                info.format = color.Format;
                info.tiling = VK_IMAGE_TILING_OPTIMAL;
                info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                info.samples = VK_SAMPLE_COUNT_1_BIT;
                info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                CreateImage(info,color);
            }

            CreateImageView(color, VK_IMAGE_ASPECT_COLOR_BIT);
            target.Colors.push_back(color);
        }

        switch (desc.DepthAttachment)
        {
            case DepthAttachmentType::Depth:
            {
                VulkanImage depth{};
                CreateDepthImage(depth, desc.Width, desc.Height, desc.DepthFormat);
                target.Depth = depth;
                break;
            }
            default:
                break;
        }

        return m_renderTargets.Register(target);
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

    std::vector<uint32_t> VulkanResourceManager::ReadBinary(std::string_view path)
    {
        std::ifstream file(path.data(), std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            LOG_ERROR("Failed to open SPIR-V file with path: {}", path);
            return {};
        }

        size_t fileSize = file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

        return buffer;
    }

    VkShaderModule VulkanResourceManager::CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        VkShaderModule module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create shader module!");
            return VK_NULL_HANDLE;
        }

        return module;
    }

    ShaderHandle VulkanResourceManager::CreateShader(std::string_view vertexSrc, std::string_view fragmentSrc)
    {
        const auto vertCode = ReadBinary(vertexSrc);
        const auto fragCode = ReadBinary(fragmentSrc);

        VkShaderModule vertexModule = CreateShaderModule(m_device.GetDevice(), vertCode);
        VkShaderModule fragmentModule = CreateShaderModule(m_device.GetDevice(), fragCode);

        if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
        {
            if (vertexModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device.GetDevice(), vertexModule, nullptr);
            }

            if (fragmentModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device.GetDevice(), fragmentModule, nullptr);
            }

            return ShaderHandle::Invalid();
        }

        VulkanShader shader{};
        shader.Vertex = vertexModule;
        shader.Fragment = fragmentModule;

        return m_shaders.Register(shader);
    }

    void VulkanResourceManager::DestroyShader(ShaderHandle handle)
    {
        if (!handle)
            return;

        auto&[VertexModule, FragmentModule] = m_shaders.Get(handle);
        VkDevice device = m_device.GetDevice();

        if (VertexModule)
        {
            vkDestroyShaderModule(device, VertexModule, nullptr);
        }

        if (FragmentModule)
        {
            vkDestroyShaderModule(device, FragmentModule, nullptr);
        }

        m_shaders.Unregister(handle);
    }

    VulkanGraphicsPipeline VulkanResourceManager::CreateGraphicsPipeline(const GraphicsPipelineInfo& info)
    {
        const VulkanShader& shader = GetShader(info.Shader);

        if (shader.Vertex == VK_NULL_HANDLE || shader.Fragment == VK_NULL_HANDLE)
        {
            LOG_ERROR("[Renderer] Cannot create Vulkan pipeline without valid shader modules");
            return {};
        }

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
        rasterInfo.cullMode = VK_CULL_MODE_NONE;                // TODO: add culling
        rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // TODO: specify front or backface
        rasterInfo.depthBiasEnable = VK_FALSE;                  // TODO: depth bias

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthState{};
        depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthState.depthTestEnable = VK_TRUE;
        depthState.depthWriteEnable = VK_TRUE;
        depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthState.depthBoundsTestEnable = VK_FALSE;
        depthState.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &colorBlendAttachment;

        VkDynamicState dynamicStates[2] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VulkanGraphicsPipeline pipeline{};
        if (vkCreatePipelineLayout(m_device.GetDevice(), &layoutInfo, nullptr, &pipeline.Layout) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer] Failed to create Vulkan pipeline layout");
            return pipeline;
        }

        const VkFormat colorAttachmentFormats[] = { VK_FORMAT_B8G8R8A8_UNORM };

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = colorAttachmentFormats;
        renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

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
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

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
        VkDevice device = m_device.GetDevice();

        for (RenderTargetHandle handle : surface.SwapchainTargetHandles)
        {
            VulkanRenderTarget& target = GetRenderTarget(handle);

            for (const auto& image : target.Colors)
            {
                vkDestroyImageView(device, image.View, nullptr);

                if (image.Memory != VK_NULL_HANDLE)
                {
                    vkDestroyImage(device, image.Image, nullptr);
                    vkFreeMemory(device, image.Memory, nullptr);
                }
            }

            if (target.Depth)
            {
                vkDestroyImageView(device, target.Depth->View, nullptr);

                if (target.Depth->Memory != VK_NULL_HANDLE)
                {
                    vkDestroyImage(device, target.Depth->Image, nullptr);
                    vkFreeMemory(device, target.Depth->Memory, nullptr);
                }
            }

            m_renderTargets.Unregister(handle);
        }

        for (VkSemaphore sem : surface.RenderFinishedSemaphores)
        {
            if (sem)
            {
                vkDestroySemaphore(device, sem,nullptr);
            }
        }

        if (surface.Swapchain)
        {
            vkDestroySwapchainKHR(device, surface.Swapchain, nullptr);
        }

        surface.SwapchainTargetHandles.clear();
        surface.ImagesInFlight.clear();
        surface.RenderFinishedSemaphores.clear();
        surface.Swapchain = VK_NULL_HANDLE;
    }

    void VulkanResourceManager::CreateSwapchainResources(VulkanSurface& surface)
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

        VkSwapchainKHR swapchain;
        if (vkCreateSwapchainKHR(m_device.GetDevice(), &createInfo, nullptr, &swapchain) != VK_SUCCESS)
        {
            LOG_ERROR("Swapchain create failed");
        }

        surface.Extent = extent;
        surface.Swapchain = swapchain;
        surface.Format = createInfo.imageFormat;

        uint32_t count = 0;

        vkGetSwapchainImagesKHR(m_device.GetDevice(),swapchain,&count,nullptr);

        std::vector<VkImage> images(count);

        vkGetSwapchainImagesKHR(m_device.GetDevice(), swapchain, &count, images.data());

        surface.SwapchainTargetHandles.resize(count);
        surface.ImagesInFlight.resize(count,VK_NULL_HANDLE);
        surface.RenderFinishedSemaphores.resize(count);

        for (uint32_t i = 0; i < count; i++)
        {
            RenderTargetDesc desc{};
            desc.Width = extent.width;
            desc.Height = extent.height;
            desc.ColorCount = 1;
            desc.ColorFormat = surface.Format;
            desc.DepthAttachment = DepthAttachmentType::Depth;
            desc.DepthFormat = VK_FORMAT_D32_SFLOAT;

            std::array<VkImage, 1> swapchainImage = { images[i] };
            surface.SwapchainTargetHandles[i] = CreateRenderTarget(desc,true, swapchainImage);

            VkSemaphoreCreateInfo sem{};
            sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            if (vkCreateSemaphore(m_device.GetDevice(),&sem, nullptr, &surface.RenderFinishedSemaphores[i]) != VK_SUCCESS)
            {
                LOG_ERROR("Render finished semaphore failed");
            }
        }
    }

    void VulkanResourceManager::CreateImage(VkImageCreateInfo info, VulkanImage &image) const
    {
        if (vkCreateImage(m_device.GetDevice(), &info, nullptr, &image.Image) != VK_SUCCESS)
        {
            LOG_ERROR("Create image failed");
        }

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(m_device.GetDevice(), image.Image, &memReq);

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = memReq.size;
        alloc.memoryTypeIndex = m_device.FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device.GetDevice(), &alloc, nullptr, &image.Memory) != VK_SUCCESS)
        {
            LOG_ERROR("[Resource Manager] Allocate image memory failed");
        }

        vkBindImageMemory(m_device.GetDevice(), image.Image, image.Memory, 0);

        image.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void VulkanResourceManager::CreateImageView(VulkanImage &image, VkImageAspectFlags aspect) const
    {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.Format;
        view.subresourceRange.aspectMask = aspect;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device.GetDevice(), &view, nullptr, &image.View) != VK_SUCCESS)
        {
            LOG_ERROR("Create image view failed");
        }
    }

    void VulkanResourceManager::CreateDepthImage(VulkanImage& image, uint32_t width, uint32_t height, VkFormat format) const
    {
        image.Format = format;

        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = { .width = width, .height = height, .depth = 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CreateImage(info, image);
        CreateImageView(image, VK_IMAGE_ASPECT_DEPTH_BIT);
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
}
