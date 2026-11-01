#pragma once

#include "crimson/renderer/draw_sort.hpp"
#include "crimson/renderer/renderer.hpp"
#include "vulkan_device.hpp"
#include "vulkan_resource_manager.hpp"

#include <tracy/TracyVulkan.hpp>

#include <array>
#include <vector>

namespace crimson::vulkan
{
    constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    struct FrameSync
    {
        vk::raii::CommandBuffer CommandBuffer{ nullptr };
        vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
        vk::raii::Fence InFlightFence{ nullptr };
    };

    // What the current command buffer has bound, so nothing gets recorded twice
    struct BoundDrawState
    {
        // Key of the last pipeline lookup, to skip the hash lookup when none of it changed
        const VulkanGraphicsPipeline* PipelineEntry = nullptr;
        ShaderHandle Shader;
        MaterialSettings Settings;
        RenderTargetHandle Target;
        VertexBufferHandle VertexBufferHandle;

        vk::Pipeline Pipeline;
        vk::PipelineLayout Layout;
        vk::Buffer VertexBuffer;
        vk::Buffer IndexBuffer;
        vk::DescriptorSet MaterialSet;
        bool GlobalsBound = false;
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

        void TransitionImage(const vk::raii::CommandBuffer& cmd, VulkanTexture& texture, vk::ImageLayout newLayout);
        void ExecuteBeginRenderPass(const vk::raii::CommandBuffer& cmdBuffer, const RenderPassInfo& info, uint32_t passIndex);
        void ExecuteEndRenderPass(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt);
        void ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawInfo& draw, RenderTargetHandle target, uint32_t passIndex);
        void ExecuteRawPass(const vk::raii::CommandBuffer& cmdBuffer, const RawPass& pass);

        void BeginRenderingOnTarget(const vk::raii::CommandBuffer& cmd, VulkanRenderTarget& rt, ClearFlags flags, const glm::vec4& clearColor, float clearDepth, uint32_t clearStencil);

        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;

        vk::raii::DescriptorSet m_globalDescriptorSet{ nullptr };

        AllocatedBuffer m_cameraUBO;
        vk::DeviceSize m_cameraUboAlignment = 0;

        AllocatedBuffer m_lightingUBO;
        vk::DeviceSize m_lightingUboAlignment = 0;

        std::array<Frame, MAX_FRAMES_IN_FLIGHT> m_frames;

        // The pool has to outlive the command buffers in m_frameSyncs
        vk::raii::CommandPool m_commandPool{ nullptr };
        std::array<FrameSync, MAX_FRAMES_IN_FLIGHT> m_frameSyncs;
        uint32_t m_currentFrameIndex = 0;

        std::vector<DrawSortEntry> m_drawOrder;
        BoundDrawState m_bound;

        TracyVkCtx m_tracyContext = nullptr;
    };
}
