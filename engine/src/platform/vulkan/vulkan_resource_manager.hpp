#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "vulkan_device.hpp"
#include "crimson/renderer/resource_manager.hpp"
#include "crimson/renderer/resource_manager_base.hpp"
#include "vulkan_resources.hpp"

namespace crimson::vulkan
{
    enum class DepthAttachmentType
    {
        None,
        Depth,
    };

    struct RenderTargetDesc
    {
        uint32_t Width = 0;
        uint32_t Height = 0;

        uint32_t ColorCount  = 0;

        DepthAttachmentType DepthAttachment{};

        VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
        VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    };

    class VulkanResourceManager: public ResourceManagerBase<VulkanResourceTraits>
    {
    public:
        explicit VulkanResourceManager(VulkanDevice& device);

        void Init();

        void Clear();
        RenderSurfaceHandle CreateRenderSurface(const Window& window) override;
        [[nodiscard]] RenderTargetHandle GetCurrentBackBuffer(RenderSurfaceHandle renderSurface) const override;

        void RecreateSwapchain(RenderSurfaceHandle handle);

        VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) override;
        void DestroyVertexBuffer(VertexBufferHandle handle) override;

        IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) override;
        void DestroyIndexBuffer(IndexBufferHandle handle) override;

        ShaderHandle CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary) override;
        void DestroyShader(ShaderHandle handle) override;

        MaterialHandle CreateMaterial(ShaderHandle shaderHandle) override;
        void DestroyMaterial(MaterialHandle handle) override;

        [[nodiscard]] VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }
        [[nodiscard]] VkDescriptorSetLayout GetCameraSetLayout() const { return m_cameraSetLayout; }

        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const;

        TextureHandle CreateTexture(const TextureInfo& info, const void* data) override;
        void DestroyTexture(TextureHandle handle) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const override;
        std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const override;

        RenderTargetHandle CreateSwapchainRenderTarget(uint32_t width, uint32_t height, VkFormat colorFormat, VkImage swapchainImage);
    protected:
        VulkanGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info) override;
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override;
    private:
        void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
        VkShaderModule CreateShaderModule(std::span<const uint32_t> code);
        void ReflectShader(VulkanShader& shader, std::span<const uint32_t> fragmentBinary);

        void CreateImage(const VkImageCreateInfo& info, VulkanTexture& texture) const;
        void CreateImageView(VulkanTexture& texture, VkImageAspectFlags aspect) const;
        TextureHandle WrapSwapchainImage(VkImage image, VkFormat format, uint32_t width, uint32_t height);

        void TransitionImageLayout(VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout) const;
        void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
        void UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const;

        void DestroySwapchainResources(VulkanSurface& surface);
        bool CreateSwapchainResources(VulkanSurface& surface);
    private:
        VulkanDevice& m_device;
        VkDescriptorSetLayout m_cameraSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    };
}
