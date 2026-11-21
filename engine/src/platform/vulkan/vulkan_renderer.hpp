#pragma once

#include "crimson/renderer/draw_sort.hpp"
#include "crimson/renderer/frame_validation.hpp"
#include "crimson/renderer/renderer.hpp"
#include "vulkan_device.hpp"
#include "vulkan_resource_manager.hpp"
#include "renderer/draw_material.hpp"

#include <tracy/TracyVulkan.hpp>

#include <array>
#include <limits>
#include <optional>
#include <span>
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
        PassKind Kind = PassKind::Scene;
        RenderTargetHandle Target;
        crimson::VertexBufferHandle VertexBufferHandle;

        vk::Pipeline Pipeline;
        vk::PipelineLayout Layout;
        vk::Buffer VertexBuffer;
        vk::Buffer IndexBuffer;
        vk::DescriptorSet MaterialSet;
        bool FrameSetBound = false;

        // The scissor of the last draw, which a draw can set apart from its view
        RenderRect Scissor;

        // What the push constant holds. A view also sets the viewport, so it starts over with every pass.
        DrawData Draw;
        bool DrawPushed = false;
    };

    // A host-visible buffer of one frame in flight that grows when its contents don't fit. The kind of buffer it is is up to
    // what grows it.
    struct FrameHostBuffer
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
        FrameHostBuffer Lights;
        FrameHostBuffer Shadows;
        FrameHostBuffer Objects;
        FrameHostBuffer Views;
        vk::raii::DescriptorSet DescriptorSet{ nullptr };

        // For the draws with material properties: each gets a material set from the pool and, when its properties write values, a
        // copy of its material's uniform block in the buffer. Both are sized once a frame, from what its draws need, before
        // any is recorded, so there is one pool and one buffer, as large as the largest frame needed.
        vk::raii::DescriptorPool PropertyPool{ nullptr };
        uint32_t PropertyPoolSets = 0;
        uint32_t PropertyPoolTextures = 0;
        FrameHostBuffer PropertyBlocks;
        vk::DeviceSize PropertyCursor = 0;
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

        // Sizes the pool and the buffer of the frame's material properties for what its draws need
        void PreparePropertyResources(InFlightFrame& frame, const MaterialPropertiesBudget& budget);

        // The material set of a draw with properties, written for this draw. None when the pool has no room left.
        [[nodiscard]] vk::DescriptorSet WritePropertySet(InFlightFrame& frame, const FramePacket& packet, const DrawItem& draw, const VulkanMaterial& material);

        // False when the buffer couldn't grow. Only for a frame whose fence has been waited on.
        bool GrowFrameBuffer(FrameHostBuffer& buffer, vk::BufferUsageFlagBits usage, vk::DeviceSize size);
        void UploadStorageBuffer(const InFlightFrame& frame, FrameHostBuffer& storage, uint32_t binding, const void* data, std::size_t size);

        void TransitionImage(const vk::raii::CommandBuffer& cmd, VulkanTexture& texture, vk::ImageLayout newLayout);
        void ExecutePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, const FramePacket& packet);
        void EndPass(const vk::raii::CommandBuffer& cmdBuffer, VulkanRenderTarget& rt);

        // passArea is where a draw lands when its view has no viewport of its own
        void ExecuteDraw(const vk::raii::CommandBuffer& cmdBuffer, const DrawItem& draw, RenderTargetHandle target, const FramePacket& packet, const RenderRect& passArea, PassKind kind);
        void ExecuteNativePass(const vk::raii::CommandBuffer& cmdBuffer, const PassRecord& pass, VulkanRenderTarget& rt);

        void BeginRendering(const vk::raii::CommandBuffer& cmd, VulkanRenderTarget& rt, const ClearInfo& clear, const std::optional<RenderRect>& area);

        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;

        // The pool has to outlive the command buffers in m_inFlight
        vk::raii::CommandPool m_commandPool{ nullptr };
        std::array<InFlightFrame, kMaxFramesInFlight> m_inFlight;
        uint32_t m_inFlightIndex = 0;

        std::vector<DrawSortEntry> m_drawOrder;
        BoundDrawState m_bound;

        // Scratch for WritePropertySet, kept to reuse its memory
        std::vector<vk::WriteDescriptorSet> m_propertyWrites;
        std::vector<vk::DescriptorImageInfo> m_propertyImages;
        vk::DescriptorBufferInfo m_propertyBuffer;
        FrameValidator m_validator;

        TracyVkCtx m_tracyContext = nullptr;
    };
}
