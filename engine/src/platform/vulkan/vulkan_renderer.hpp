#pragma once

#include "crimson/renderer/draw_sort.hpp"
#include "crimson/renderer/frame_validation.hpp"
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
    // What the current command buffer has bound, so nothing gets recorded twice
    struct BoundDrawState
    {
        // Key of the last pipeline lookup, to skip the hash lookup when none of it changed
        const VulkanGraphicsPipeline* PipelineEntry = nullptr;
        ShaderHandle Shader;
        MaterialSettings Settings;
        ShaderVariant Variant = ShaderVariant::Main;
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

    // One frame in flight: its command buffer, what it waits on, and what its frame descriptor set points at
    struct InFlightFrame
    {
        vk::raii::CommandBuffer CommandBuffer{ nullptr };
        vk::raii::Semaphore ImageAvailableSemaphore{ nullptr };
        vk::raii::Fence InFlightFence{ nullptr };

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

        [[nodiscard]] bool CanRender() const override;
        bool Submit(FrameRecorder frame) override;

        void SetShadowAtlas(TextureHandle shadowAtlas) override;
    private:
        void InitializeSynchronizationAndCommands();
        void InitGlobals();

        // Waits for the frame's slot, uploads its data, acquires the next swapchain image and starts the command buffer.
        // False when the frame has to be dropped.
        bool BeginFrame(const FramePacket& packet);
        void EndFrame();

        void UploadStorageBuffer(const InFlightFrame& frame, FrameStorageBuffer& storage, uint32_t binding, const void* data, std::size_t size);

        void TransitionImage(const vk::raii::CommandBuffer& cmd, VulkanTexture& texture, vk::ImageLayout newLayout);
        void ExecutePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, uint32_t passIndex);
        void BeginPass(const vk::raii::CommandBuffer& cmdBuffer, const PassInfo& info, VulkanRenderTarget& rt, uint32_t passIndex);
        void EndPass(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt);
        void ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawItem& draw, RenderTargetHandle target, uint32_t passIndex);
        void ExecuteNativePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, VulkanRenderTarget& rt);

        void BeginRendering(const vk::raii::CommandBuffer& cmd, VulkanRenderTarget& rt, const ClearInfo& clear, const std::optional<RenderRect>& area);

        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;

        vk::raii::DescriptorSet m_viewDescriptorSet{ nullptr };
        AllocatedBuffer m_viewUBO;
        vk::DeviceSize m_viewUboAlignment = 0;

        // The pool has to outlive the command buffers in m_inFlight
        vk::raii::CommandPool m_commandPool{ nullptr };
        std::array<InFlightFrame, kMaxFramesInFlight> m_inFlight;
        uint32_t m_inFlightIndex = 0;

        std::vector<DrawSortEntry> m_drawOrder;
        BoundDrawState m_bound;
        FrameValidator m_validator;

        TracyVkCtx m_tracyContext = nullptr;
    };
}
