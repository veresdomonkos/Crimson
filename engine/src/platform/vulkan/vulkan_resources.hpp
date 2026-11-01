#pragma once

#include <optional>
#include <vector>
#include <unordered_map>

#include "vulkan_include.hpp"

#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/material_settings.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "shader/shader_reflection.hpp"
#include "vulkan_allocator.hpp"

namespace crimson::vulkan
{
    struct VulkanTexture
    {
        vk::Image Image;
        vk::raii::ImageView View{ nullptr };
        vk::raii::ImageView AttachmentView{ nullptr }; // only for depth-stencil formats, View is depth-only there
        AllocatedImage Allocation; // empty for swapchain images, which the swapchain owns

        vk::Format Format = vk::Format::eUndefined;
        vk::ImageLayout Layout = vk::ImageLayout::eUndefined;
        vk::ImageAspectFlags Aspect = vk::ImageAspectFlagBits::eColor;

        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    struct VulkanSurface
    {
        vk::raii::SwapchainKHR Swapchain{ nullptr };

        uint32_t CurrentImageIndex = 0;

        vk::Extent2D Extent{};
        vk::Format Format = vk::Format::eUndefined;

        std::vector<RenderTargetHandle> SwapchainTargetHandles;

        std::vector<vk::raii::Semaphore> RenderFinishedSemaphores;

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
        vk::DeviceSize Size = 0;
        BufferLayout Layout;
        BufferUsage Usage{};
    };

    struct VulkanIndexBuffer
    {
        AllocatedBuffer Buffer;
        vk::DeviceSize Size = 0;
        IndexType Type{};
        BufferUsage Usage{};
    };

    struct VulkanShader
    {
        vk::raii::ShaderModule Vertex{ nullptr };
        vk::raii::ShaderModule Fragment{ nullptr };

        vk::raii::DescriptorSetLayout MaterialSetLayout{ nullptr };
        vk::raii::PipelineLayout PipelineLayout{ nullptr };

        ShaderInterface Interface;
    };

    struct VulkanGraphicsPipeline
    {
        vk::PipelineLayout Layout; // owned by the shader
        vk::raii::Pipeline Pipeline{ nullptr };
    };

    struct VulkanMaterial
    {
        ShaderHandle Shader;
        MaterialSettings Settings;
        AllocatedBuffer UniformBuffer;
        vk::DeviceSize UniformBufferSize = 0;
        vk::raii::DescriptorSet DescriptorSet{ nullptr };
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
    };
}
