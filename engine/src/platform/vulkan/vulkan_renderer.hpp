#pragma once

#include "vulkan_resource_manager.hpp"
#include "crimson/core/window.hpp"
#include "crimson/renderer/frame.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson::vulkan
{
    class VulkanRenderer : public Renderer
    {
    public:
        VulkanRenderer() : m_resourceManager(m_device), m_commandBuffer(2 * 1024 * 1024) {}

        RenderSurfaceHandle Initialize(const Window& primaryWindow) override;
        void Shutdown() override;
        ResourceManager& GetResourceManager() override;
        std::optional<Frame> BeginFrame(RenderSurfaceHandle surface) override;
        void EndFrame(Frame& frame) override;
    private:
        void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlagBits flagBits, VkImageLayout& currentLayout, VkImageLayout newLayout);
        void InitializeSynchronizationAndCommands();
        void ExecuteBeginRenderPass(VkCommandBuffer cmdBuffer, const BeginRenderPassCommand& cmd);
        void ExecuteEndRenderPass(VkCommandBuffer cmdBuffer, VulkanRenderTarget& rt);
    private:
        VulkanDevice m_device;
        VulkanResourceManager m_resourceManager;
        RenderCommandBuffer m_commandBuffer;

        const int MAX_FRAMES_IN_FLIGHT = 2;
        uint32_t m_currentFrameIndex = 0;

        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        std::vector<VkCommandBuffer> m_vkCommandBuffers;
        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        std::vector<VkFence> m_inFlightFences;
    };
}
