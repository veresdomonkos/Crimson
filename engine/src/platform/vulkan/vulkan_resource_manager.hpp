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

        MaterialHandle CreateMaterial(ShaderHandle shaderHandle) override;
        void DestroyMaterial(MaterialHandle handle) override;

        void SetMaterialTexture(MaterialHandle handle, std::string_view name, TextureHandle texture);

        TextureHandle CreateTexture(const TextureInfo& info, const void* data) override;
        void DestroyTexture(TextureHandle handle) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const override;
        std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const override;

        [[nodiscard]] VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }
        [[nodiscard]] VkDescriptorSetLayout GetGlobalSetLayout() const { return m_globalSetLayout; }
        [[nodiscard]] VkSampler GetDefaultSampler() const { return m_defaultSampler; }

        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const;
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

        RenderTargetHandle CreateSwapchainRenderTarget(uint32_t width, uint32_t height, VkFormat colorFormat, VkImage swapchainImage);
        TextureHandle WrapSwapchainImage(VkImage image, VkFormat format, uint32_t width, uint32_t height);

        VkShaderModule CreateShaderModule(std::span<const uint32_t> code);
        void ReflectShader(VulkanShader& shader, std::span<const uint32_t> binary, VkShaderStageFlagBits stage);

        bool CreateImage(const VkImageCreateInfo& info, VulkanTexture& texture) const;
        bool CreateImageView(VulkanTexture& texture, VkImageAspectFlags aspect) const;

        void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
        void TransitionImageLayout(VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout) const;
        void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
        void UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const;

    private:
        VulkanDevice& m_device;
        VkDescriptorSetLayout m_globalSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkSampler m_defaultSampler = VK_NULL_HANDLE;
        VulkanSurface m_primarySurface;;
    };
}