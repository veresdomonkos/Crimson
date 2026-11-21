#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include <unordered_map>

#include "vulkan_include.hpp"

#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/frame_recorder.hpp"
#include "crimson/renderer/material_settings.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/texture.hpp"
#include "renderer/draw_material.hpp"
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

        // What it was created with: its size, and the rules of updating it
        TextureInfo Info;
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

    // How many copies of a Dynamic or Stream buffer there are: one for each frame in flight, which may still be reading it,
    // and one for the frame being recorded
    constexpr size_t kBufferVersions = kMaxFramesInFlight + 1;

    // The copies of one buffer. A Static buffer has one, and the others one per version.
    struct VulkanBufferCopies
    {
        std::vector<AllocatedBuffer> Versions;
        uint32_t Current = 0;
        uint64_t UpdatedFrame = std::numeric_limits<uint64_t>::max();

        // What a draw reads
        [[nodiscard]] vk::Buffer Get() const { return Versions[Current].Get(); }
        explicit operator bool() const { return !Versions.empty(); }

        // What an update writes. The first update of a frame moves on to the next copy, which no frame in flight reads:
        // the frames before this one read at most the two copies before the current one.
        [[nodiscard]] AllocatedBuffer& ForUpdate(uint64_t frameNumber)
        {
            if (UpdatedFrame != frameNumber)
            {
                Current = static_cast<uint32_t>((Current + 1) % Versions.size());
                UpdatedFrame = frameNumber;
            }

            return Versions[Current];
        }
    };

    struct VulkanVertexBuffer
    {
        VulkanBufferCopies Buffer;
        vk::DeviceSize Size = 0;
        BufferLayout Layout;
        BufferUsage Usage{};
    };

    struct VulkanIndexBuffer
    {
        VulkanBufferCopies Buffer;
        vk::DeviceSize Size = 0;
        IndexType Type{};
        BufferUsage Usage{};
    };

    struct VulkanShader
    {
        vk::raii::ShaderModule Vertex{ nullptr };
        vk::raii::ShaderModule Fragment{ nullptr };
        vk::raii::ShaderModule FragmentDepth{ nullptr }; // null without PSDepth, and then a depth pass has no fragment stage

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

        // What was set with SetMaterialTexture, by slot, for the draws that have properties
        MaterialTextures Textures;
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
