#pragma once

#include "crimson/renderer/draw_sort.hpp"
#include "crimson/renderer/renderer.hpp"
#include "vulkan_device.hpp"
#include "vulkan_resource_manager.hpp"

#include <tracy/TracyVulkan.hpp>

#include <array>
#include <limits>
#include <optional>
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
        crimson::VertexBufferHandle VertexBufferHandle;

        vk::Pipeline Pipeline;
        vk::PipelineLayout Layout;
        vk::Buffer VertexBuffer;
        vk::Buffer IndexBuffer;
        vk::DescriptorSet MaterialSet;
        bool FrameSetBound = false;
        bool ViewSetBound = false;
        ObjectId Object = std::numeric_limits<ObjectId>::max();
    };

    // A host-visible buffer of one frame in flight that grows when its contents don't fit
    struct FrameStorageBuffer
    {
        AllocatedBuffer Buffer;
        vk::DeviceSize Capacity = 0;
    };

    // What the frame descriptor set of one frame in flight points at
    struct FrameResources
    {
        AllocatedBuffer FrameBlockBuffer;
        FrameStorageBuffer Lights;
        FrameStorageBuffer Shadows;
        FrameStorageBuffer Objects;
        vk::raii::DescriptorSet DescriptorSet{ nullptr };
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
        void UploadStorageBuffer(const FrameResources& resources, FrameStorageBuffer& storage, uint32_t binding, const void* data, std::size_t size);

        void TransitionImage(const vk::raii::CommandBuffer& cmd, VulkanTexture& texture, vk::ImageLayout newLayout);
        void ExecuteBeginRenderPass(const vk::raii::CommandBuffer& cmdBuffer, const RenderPassInfo& info, uint32_t passIndex);
        void ExecuteEndRenderPass(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt);
        void ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawInfo& draw, RenderTargetHandle target, uint32_t passIndex);
        void ExecuteRawPass(const vk::raii::CommandBuffer& cmdBuffer, const RawPass& pass);

        void BeginRenderingOnTarget(const vk::raii::CommandBuffer& cmd, VulkanRenderTarget& rt, ClearFlags flags, const glm::vec4& clearColor, float clearDepth, uint32_t clearStencil, const std::optional<RenderRect>& area);

        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;

        std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> m_frameResources;

        vk::raii::DescriptorSet m_viewDescriptorSet{ nullptr };
        AllocatedBuffer m_viewUBO;
        vk::DeviceSize m_viewUboAlignment = 0;

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
