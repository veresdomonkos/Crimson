#pragma once

#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/resource_handles.hpp"

namespace crimson::vulkan
{
    struct VulkanSurface
    {
        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        VkSwapchainKHR Swapchain = VK_NULL_HANDLE;

        uint32_t CurrentImageIndex = 0;

        VkExtent2D Extent{};
        VkFormat Format = VK_FORMAT_UNDEFINED;

        std::vector<RenderTargetHandle> SwapchainTargetHandles;

        std::vector<VkFence> ImagesInFlight;
        std::vector<VkSemaphore> RenderFinishedSemaphores;
    };

    struct VulkanImage
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;

        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct VulkanRenderTarget
    {
        std::vector<VulkanImage> Colors;
        std::optional<VulkanImage> Depth;

        uint32_t Width = 0;
        uint32_t Height = 0;

        bool IsSwapchain = false;
    };

    struct VulkanVertexBuffer
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Size = 0;
        BufferLayout Layout;
        BufferUsage Usage{};
    };

    struct VulkanIndexBuffer
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Size = 0;
        IndexType Type{};
        BufferUsage Usage{};
    };

    struct VulkanShader
    {
        VkShaderModule Vertex = VK_NULL_HANDLE;
        VkShaderModule Fragment = VK_NULL_HANDLE;

        VkDescriptorSetLayout MaterialSetLayout = VK_NULL_HANDLE;
        size_t MaterialUniformSize = 0;

        std::unordered_map<std::string, ShaderPropertyInfo> Properties{};
    };

    struct VulkanGraphicsPipeline
    {
        VkPipelineLayout Layout = VK_NULL_HANDLE;
        VkPipeline Pipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    };

    struct VulkanMaterial
    {
        ShaderHandle Shader;
        VkBuffer UniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory UniformBufferMemory = VK_NULL_HANDLE;
        void* MappedData = nullptr;
        VkDeviceSize UniformBufferSize = 0;
        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    };

    struct VulkanResourceTraits
    {
        using RenderSurface = VulkanSurface;
        using RenderTarget = VulkanRenderTarget;
        using VertexBuffer = VulkanVertexBuffer;
        using IndexBuffer = VulkanIndexBuffer;
        using Shader = VulkanShader;
        using Material = VulkanMaterial;
        using GraphicsPipeline = VulkanGraphicsPipeline;
    };
}
