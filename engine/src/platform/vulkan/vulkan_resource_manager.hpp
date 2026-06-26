#pragma once

#include <span>

#include "vulkan_swapchain.hpp"
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
        RenderTargetHandle GetCurrentBackBuffer(RenderSurfaceHandle renderSurface) const override;

        void RecreateSwapchain(RenderSurfaceHandle handle);
    private:
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc, bool isSwapchain, std::span<const VkImage> swapchainImages);
        void DestroySwapchainResources(VulkanSurface& surface);
        void CreateSwapchainResources(VulkanSurface& surface);
        void CreateImage(VkImageCreateInfo info, VulkanImage& image);
        void CreateImageView(VulkanImage& image, VkImageAspectFlags aspect);
        void CreateDepthImage(VulkanImage& image, uint32_t width, uint32_t height, VkFormat format);
    private:
        VulkanDevice& m_device;
    };
}
