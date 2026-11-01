#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "vulkan_device.hpp"
#include "crimson/renderer/resource_manager_base.hpp"
#include "vulkan_resources.hpp"

namespace crimson::vulkan
{
    class VulkanResourceManager : public ResourceManagerBase<VulkanResourceTraits>
    {
    public:
        explicit VulkanResourceManager(VulkanDevice& device);
        ~VulkanResourceManager() override;

        VulkanResourceManager(const VulkanResourceManager&) = delete;
        VulkanResourceManager& operator=(const VulkanResourceManager&) = delete;
        VulkanResourceManager(VulkanResourceManager&&) = delete;
        VulkanResourceManager& operator=(VulkanResourceManager&&) = delete;

        [[nodiscard]] VulkanSurface& GetRenderSurface() { return m_primarySurface; }

        [[nodiscard]] RenderTargetHandle GetCurrentBackBuffer() const;
        bool RecreateSwapchain(VulkanSurface& surface);

        VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) override;
        void DestroyVertexBuffer(VertexBufferHandle handle) override;

        IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) override;
        void DestroyIndexBuffer(IndexBufferHandle handle) override;

        ShaderHandle CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary) override;
        void DestroyShader(ShaderHandle handle) override;

        MaterialHandle CreateMaterial(ShaderHandle shaderHandle, const MaterialSettings& settings) override;
        void DestroyMaterial(MaterialHandle handle) override;

        void SetMaterialTexture(MaterialHandle handle, std::string_view name, TextureHandle texture);

        TextureHandle CreateTexture(const TextureInfo& info, const void* data) override;
        void DestroyTexture(TextureHandle handle) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const override;
        std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const override;

        [[nodiscard]] vk::DescriptorPool GetDescriptorPool() const { return *m_descriptorPool; }
        [[nodiscard]] vk::DescriptorSetLayout GetGlobalSetLayout() const { return *m_globalSetLayout; }
        [[nodiscard]] vk::Sampler GetDefaultSampler() const { return *m_defaultSampler; }
    protected:
        VulkanGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info) override;
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override;

    private:
        void CreateDescriptorPool();
        void CreateGlobalSetLayout();
        void CreateDefaultSampler();

        void DestroyResources();

        bool CreateSwapchainResources(VulkanSurface& surface);
        void DestroySwapchainResources(VulkanSurface& surface);

        RenderTargetHandle CreateSwapchainRenderTarget(uint32_t width, uint32_t height, vk::Format colorFormat, vk::Image swapchainImage);
        TextureHandle WrapSwapchainImage(vk::Image image, vk::Format format, uint32_t width, uint32_t height);

        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(std::span<const uint32_t> code) const;
        void ReflectShader(VulkanShader& shader, std::span<const uint32_t> binary);

        AllocatedBuffer CreateBufferWithData(vk::DeviceSize size, vk::BufferUsageFlags usage, BufferUsage bufferUsage, const void* data) const;
        void CopyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) const;
        void UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const;

    private:
        VulkanDevice& m_device;
        vk::raii::DescriptorSetLayout m_globalSetLayout{ nullptr };
        vk::raii::DescriptorPool m_descriptorPool{ nullptr };
        vk::raii::Sampler m_defaultSampler{ nullptr };
        VulkanSurface m_primarySurface;
    };
}
