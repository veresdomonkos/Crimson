#pragma once
#include "crimson/renderer/graphics_pipeline_cache.hpp"
#include "crimson/renderer/handle_registry.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    template <typename PlatformResourceTraits>
    class ResourceManagerBase : public ResourceManager
    {
    private:
        using RenderSurface = PlatformResourceTraits::RenderSurface;
        using RenderTarget = PlatformResourceTraits::RenderTarget;
        using VertexBuffer = PlatformResourceTraits::VertexBuffer;
        using IndexBuffer = PlatformResourceTraits::IndexBuffer;
        using GraphicsPipeline = PlatformResourceTraits::GraphicsPipeline;
    public:
        RenderSurface& GetRenderSurface(RenderSurfaceHandle handle) { return m_renderSurfaces.Get(handle); }
        RenderTarget& GetRenderTarget(RenderTargetHandle handle) { return m_renderTargets.Get(handle); }
        VertexBuffer& GetVertexBuffer(VertexBufferHandle handle) { return m_vertexBuffers.Get(handle); }
        IndexBuffer& GetIndexBuffer(IndexBufferHandle handle) { return m_indexBuffers.Get(handle); }

        GraphicsPipeline& GetOrCreateGraphicsPipeline(const GraphicsPipelineInfo& info)
        {
            auto it = m_graphicsPipelines.find(info);
            if (it != m_graphicsPipelines.end())
                return it->second;

            auto [newIt, inserted] = m_graphicsPipelines.emplace(info, CreateGraphicsPipeline(info));
            return newIt->second;
        }
    protected:
        virtual GraphicsPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info) = 0;
    protected:
        HandleRegistry<RenderSurfaceHandle, RenderSurface> m_renderSurfaces;
        HandleRegistry<RenderTargetHandle, RenderTarget> m_renderTargets;
        HandleRegistry<VertexBufferHandle, VertexBuffer> m_vertexBuffers;
        HandleRegistry<IndexBufferHandle, IndexBuffer> m_indexBuffers;
        GraphicsPipelineCache<GraphicsPipeline> m_graphicsPipelines;
    };
}
