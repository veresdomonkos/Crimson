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
    protected:
        VulkanGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info) override;
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override;
    private:
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc, bool isSwapchain, std::span<const VkImage> swapchainImages);
        void DestroySwapchainResources(VulkanSurface& surface);
        bool CreateSwapchainResources(VulkanSurface& surface);
        void CreateImage(VkImageCreateInfo info, VulkanImage& image) const;
        void CreateImageView(VulkanImage& image, VkImageAspectFlags aspect) const;
        void CreateDepthImage(VulkanImage& image, uint32_t width, uint32_t height, VkFormat format) const;
        void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
        VkShaderModule CreateShaderModule(std::span<const uint32_t> code);
        void ReflectShader(VulkanShader& shader, std::span<const uint32_t> fragmentBinary);
    private:
        VulkanDevice& m_device;
        VkDescriptorSetLayout m_cameraSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    };
}
