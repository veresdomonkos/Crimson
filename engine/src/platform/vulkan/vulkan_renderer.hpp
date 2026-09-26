#pragma once

#include "vulkan_resource_manager.hpp"
#include "crimson/core/window.hpp"
#include "crimson/renderer/frame_context.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/resource_manager.hpp"
#include <array>

namespace crimson::vulkan
{
    struct FrameSync
    {
        VkCommandBuffer CommandBuffer{};
        VkSemaphore ImageAvailableSemaphore{};
        VkFence InFlightFence{};
    };

    class VulkanRenderer : public Renderer
    {
    public:
        VulkanRenderer() : m_resourceManager(m_device) {}

        RenderSurfaceHandle Initialize(const Window& primaryWindow) override;
        void Shutdown() override;
        ResourceManager& GetResourceManager() override;
        FrameContext BeginFrame(RenderSurfaceHandle surfaceHandle, const FrameLightingData& lighting) override;
        void EndFrame(const FrameContext& frame) override;

        void SetShadowMap(TextureHandle shadowMap) override;
    private:
        void InitGlobals();
        void TransitionImage(VkCommandBuffer cmd, VulkanTexture& texture, VkImageAspectFlagBits flagBits, VkImageLayout newLayout);
        void InitializeSynchronizationAndCommands();
        void ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const RenderPassInfo& info, uint32_t passIndex);
        void ExecuteEndRenderPass(VkCommandBuffer cmdBuffer, VulkanRenderTarget& rt);
        void ExecuteDraw(VkCommandBuffer cmdBuffer, const DrawInfo& draw, RenderTargetHandle target, uint32_t passIndex);
    private:
        constexpr static int MAX_FRAMES_IN_FLIGHT = 2;

        VulkanDevice m_device;
        VulkanResourceManager m_resourceManager{m_device};

        VkDescriptorSet m_globalDescriptorSet = VK_NULL_HANDLE;

        // CameraBlock
        VkBuffer m_cameraUBOBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_cameraUBOBufferMemory = VK_NULL_HANDLE;
        void* m_cameraMappedData = nullptr;
        VkDeviceSize m_cameraUboAlignment = 0;

        // LightingBlock
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
