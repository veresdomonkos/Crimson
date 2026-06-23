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
        Frame BeginFrame(RenderSurfaceHandle surface) override;
        void EndFrame(Frame& frame) override;

    private:
        VulkanDevice m_device;
        VulkanResourceManager m_resourceManager;
        RenderCommandBuffer m_commandBuffer;
    };
}
