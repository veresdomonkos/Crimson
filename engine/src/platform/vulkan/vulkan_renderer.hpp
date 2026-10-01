#pragma once

#include "crimson/renderer/renderer.hpp"
#include "vulkan_device.hpp"
#include "vulkan_resource_manager.hpp"

#include <array>

namespace crimson::vulkan
{
    constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    struct FrameSync
    {
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence InFlightFence = VK_NULL_HANDLE;
    };

    class VulkanRenderer : public Renderer
    {
    public:
        VulkanRenderer(VulkanDevice& device, VulkanResourceManager& resourceManager);
        ~VulkanRenderer() override;

        VulkanRenderer(const VulkanRenderer&) = delete;
        VulkanRenderer& operator=(const VulkanRenderer&) = delete;
        VulkanRenderer(VulkanRenderer&&) = delete;
        VulkanRenderer& operator=(VulkanRenderer&&) = delete;

        FrameContext BeginFrame(const FrameLightingData& lighting) override;
        void EndFrame(const FrameContext& frameContext) override;

        void SetShadowMap(TextureHandle shadowMap) override;
    private:
        void InitializeSynchronizationAndCommands();
        void InitGlobals();

        void TransitionImage(VkCommandBuffer cmd, VulkanTexture& texture, VkImageAspectFlagBits flagBits, VkImageLayout newLayout);
        void ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const RenderPassInfo& info, uint32_t passIndex);
        void ExecuteEndRenderPass(VkCommandBuffer cmdBuffer, VulkanRenderTarget& rt);
        void ExecuteDraw(VkCommandBuffer cmdBuffer, const DrawInfo& draw, RenderTargetHandle target, uint32_t passIndex);
        void ExecuteRawPass(VkCommandBuffer cmdBuffer, const RawPass& pass);

        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;

        VkDescriptorSet m_globalDescriptorSet = VK_NULL_HANDLE;

        VkBuffer m_cameraUBOBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_cameraUBOBufferMemory = VK_NULL_HANDLE;
        void* m_cameraMappedData = nullptr;
        VkDeviceSize m_cameraUboAlignment = 0;

        VkBuffer m_lightingUBOBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_lightingUBOBufferMemory = VK_NULL_HANDLE;
        void* m_lightingMappedData = nullptr;
        VkDeviceSize m_lightingUboAlignment = 0;

        std::array<Frame, MAX_FRAMES_IN_FLIGHT> m_frames;
        std::array<FrameSync, MAX_FRAMES_IN_FLIGHT> m_frameSyncs;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        uint32_t m_currentFrameIndex = 0;
    };
}