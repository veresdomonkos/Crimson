#pragma once

#include "vulkan_resource_manager.hpp"
#include "crimson/core/window.hpp"
#include "crimson/renderer/frame_context.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/resource_manager.hpp"

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
        FrameContext BeginFrame(RenderSurfaceHandle surface) override;
        void EndFrame(const FrameContext& frame) override;
    private:
        void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlagBits flagBits, VkImageLayout& currentLayout, VkImageLayout newLayout);
        void InitializeSynchronizationAndCommands();
        void ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const RenderPassInfo& info);
        void ExecuteEndRenderPass(VkCommandBuffer cmdBuffer, VulkanRenderTarget& rt);
    private:
        VulkanDevice m_device;
        VulkanResourceManager m_resourceManager;

        constexpr static int MAX_FRAMES_IN_FLIGHT = 2;
        uint32_t m_currentFrameIndex = 0;

        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        std::array<FrameSync, MAX_FRAMES_IN_FLIGHT> m_frameSyncs;
        std::array<Frame, MAX_FRAMES_IN_FLIGHT> m_frames;
    };
}
