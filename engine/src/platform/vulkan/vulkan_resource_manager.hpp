#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "vulkan_device.hpp"
#include "crimson/renderer/resource_manager_base.hpp"
#include "vulkan_pipeline_cache.hpp"
#include "vulkan_resources.hpp"

namespace crimson::vulkan
{
    // The stages that read the push constant: the vertex shader for the object and the view, the pixel shader for the view
    constexpr vk::ShaderStageFlags kDrawDataStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

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

        ShaderHandle CreateShader(const ShaderBinary& binary) override;
        void DestroyShader(ShaderHandle handle) override;
        [[nodiscard]] const ShaderInterface& GetShaderInterface(ShaderHandle handle) const override { return m_shaders.Get(handle).Interface; }

        MaterialHandle CreateMaterial(ShaderHandle shaderHandle, const MaterialSettings& settings) override;
        void DestroyMaterial(MaterialHandle handle) override;
        void SetMaterialSettings(MaterialHandle handle, const MaterialSettings& settings) override { m_materials.Get(handle).Settings = settings; }

        void SetMaterialTexture(MaterialHandle handle, std::string_view name, TextureHandle texture) override;

        TextureHandle CreateTexture(const TextureInfo& info, const void* data) override;
        void DestroyTexture(TextureHandle handle) override;
        std::vector<std::byte> ReadColorPixels(TextureHandle texture) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo& info) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        TextureHandle GetColorAttachment(RenderTargetHandle handle, uint32_t index) const override;
        std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle handle) const override;

        [[nodiscard]] vk::DescriptorPool GetDescriptorPool() const { return *m_descriptorPool; }
        [[nodiscard]] vk::DescriptorSetLayout GetFrameSetLayout() const { return *m_frameSetLayout; }

        VulkanGraphicsPipeline& GetOrCreateGraphicsPipeline(const GraphicsPipelineInfo& info);
    protected:
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override;

    private:
        void CreateDescriptorPool();
        void CreateSamplers();
        void CreateStandardSetLayouts();
        void CreateDefaultTexture();

        void DestroyResources();

        VulkanGraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info);

        bool CreateSwapchainResources(VulkanSurface& surface);
        void DestroySwapchainResources(VulkanSurface& surface);

        RenderTargetHandle CreateSwapchainRenderTarget(uint32_t width, uint32_t height, vk::Format colorFormat, vk::Image swapchainImage);
        TextureHandle WrapSwapchainImage(vk::Image image, vk::Format format, uint32_t width, uint32_t height);

        [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(std::span<const uint32_t> code) const;
        void WriteMaterialTexture(vk::DescriptorSet set, uint32_t binding, const VulkanTexture& texture) const;

        AllocatedBuffer CreateBufferWithData(vk::DeviceSize size, vk::BufferUsageFlags usage, BufferUsage bufferUsage, const void* data) const;
        void CopyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) const;
        void UploadTextureData(VulkanTexture& texture, const TextureInfo& info, const void* data) const;

    private:
        VulkanDevice& m_device;
        vk::raii::Sampler m_linearWrapSampler{ nullptr };
        vk::raii::Sampler m_linearClampSampler{ nullptr };
        vk::raii::Sampler m_pointClampSampler{ nullptr };
        vk::raii::Sampler m_shadowCompareSampler{ nullptr };
        vk::raii::DescriptorSetLayout m_frameSetLayout{ nullptr };
        vk::raii::DescriptorSetLayout m_unusedSetLayout{ nullptr };
        vk::raii::DescriptorPool m_descriptorPool{ nullptr };
        TextureHandle m_defaultTexture; // what a material's textures show until they are set
        VulkanSurface m_primarySurface;
        GraphicsPipelineCache<VulkanGraphicsPipeline> m_graphicsPipelines;
    };
}
