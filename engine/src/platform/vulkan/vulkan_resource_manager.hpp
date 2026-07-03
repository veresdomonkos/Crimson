#pragma once

#include <span>

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
        explicit VulkanResourceManager(VulkanDevice& device) : m_device(device) {}
        void Clear();
        RenderSurfaceHandle CreateRenderSurface(const Window& window) override;
        [[nodiscard]] RenderTargetHandle GetCurrentBackBuffer(RenderSurfaceHandle renderSurface) const override;

        void RecreateSwapchain(RenderSurfaceHandle handle);

        VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo& info, const void* data) override;
        void DestroyVertexBuffer(VertexBufferHandle handle) override;

        IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo& info, const void* data) override;
        void DestroyIndexBuffer(IndexBufferHandle handle) override;

        std::vector<uint32_t> ReadBinary(std::string_view path); // temp
        VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t> &code); // temp?

        ShaderHandle CreateShader(std::string_view vertexSrc, std::string_view fragmentSrc) override;
        void DestroyShader(ShaderHandle handle) override;
    protected:
        VulkanGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info) override;
    private:
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc, bool isSwapchain, std::span<const VkImage> swapchainImages);
        void DestroySwapchainResources(VulkanSurface& surface);
        void CreateSwapchainResources(VulkanSurface& surface);
        void CreateImage(VkImageCreateInfo info, VulkanImage& image) const;
        void CreateImageView(VulkanImage& image, VkImageAspectFlags aspect) const;
        void CreateDepthImage(VulkanImage& image, uint32_t width, uint32_t height, VkFormat format) const;
        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const;
        void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
    private:
        VulkanDevice& m_device;
    };
}
