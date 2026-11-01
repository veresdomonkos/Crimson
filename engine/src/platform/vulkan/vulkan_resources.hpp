#pragma once

#include <optional>
#include <vector>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "vulkan_allocator.hpp"

namespace crimson::vulkan
{
    struct VulkanTexture
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkImageView AttachmentView = VK_NULL_HANDLE; // only for depth-stencil formats, View is depth-only there
        AllocatedImage Allocation; // empty for swapchain images, which the swapchain owns

        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    struct VulkanSurface
    {
        VkSwapchainKHR Swapchain = VK_NULL_HANDLE;

        uint32_t CurrentImageIndex = 0;

        VkExtent2D Extent{};
        VkFormat Format = VK_FORMAT_UNDEFINED;

        std::vector<RenderTargetHandle> SwapchainTargetHandles;

        std::vector<VkSemaphore> RenderFinishedSemaphores;

        bool IsDirty = false;
    };

    struct VulkanRenderTarget
    {
        std::vector<TextureHandle> ColorAttachments;
        TextureHandle DepthAttachment;

        uint32_t Width = 0;
        uint32_t Height = 0;

        bool IsSwapchain = false;
    };

    struct VulkanVertexBuffer
    {
        AllocatedBuffer Buffer;
        VkDeviceSize Size = 0;
        BufferLayout Layout;
        BufferUsage Usage{};
    };

    struct VulkanIndexBuffer
    {
        AllocatedBuffer Buffer;
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
        uint32_t MaterialUboBinding = 0;

        std::unordered_map<std::string, ShaderPropertyInfo> Properties{};
        std::unordered_map<std::string, ShaderTextureBinding> TextureBindings{};
    };

    struct VulkanGraphicsPipeline
    {
        VkPipelineLayout Layout = VK_NULL_HANDLE;
        VkPipeline Pipeline = VK_NULL_HANDLE;
    };

    struct VulkanMaterial
    {
        ShaderHandle Shader;
        AllocatedBuffer UniformBuffer;
        VkDeviceSize UniformBufferSize = 0;
        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    };

    struct VulkanResourceTraits
    {
        using RenderSurface   = VulkanSurface;
        using RenderTarget    = VulkanRenderTarget;
        using Texture         = VulkanTexture;
        using VertexBuffer    = VulkanVertexBuffer;
        using IndexBuffer     = VulkanIndexBuffer;
        using Shader          = VulkanShader;
        using Material        = VulkanMaterial;
        using GraphicsPipeline = VulkanGraphicsPipeline;
    };
}