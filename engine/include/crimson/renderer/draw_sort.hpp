#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

#include "crimson/renderer/frame_context.hpp"
#include "crimson/renderer/material_settings.hpp"

namespace crimson
{
    struct DrawSortEntry
    {
        uint64_t Key = 0;
        uint32_t Index = 0; // into the pass's draws
    };

    // From the highest bits down: transparent (1), shader (12), cull (2), material (17), vertex buffer (16), index buffer (16).
    // Ids too large for their field wrap around, which can only cost extra binds, the draws still use their own handles.
    // Transparent draws only set the top bit so the index keeps them in submission order, they have no depth to sort by yet.
    template <typename Material>
    uint64_t MakeDrawSortKey(const DrawInfo& draw, const Material& material)
    {
        if (material.Settings.RenderMode == MaterialRenderMode::Transparent)
            return uint64_t{1} << 63;

        const auto field = [](uint32_t value, uint32_t bits, uint32_t shift)
        {
            return (static_cast<uint64_t>(value) & ((uint64_t{1} << bits) - 1)) << shift;
        };

        return field(material.Shader.GetId(), 12, 51)
             | field(static_cast<uint32_t>(material.Settings.Cull), 2, 49)
             | field(draw.Material.GetId(), 17, 32)
             | field(draw.VertexBuffer.GetId(), 16, 16)
             | field(draw.IndexBuffer.GetId(), 16, 0);
    }

    // Opaque draws first, grouped by state so the renderers rebind as little as possible.
    template <typename ResourceManager>
    void BuildDrawOrder(std::span<const DrawInfo> draws, const ResourceManager& resourceManager, std::vector<DrawSortEntry>& order)
    {
        order.clear();
        order.reserve(draws.size());

        for (uint32_t i = 0; i < draws.size(); ++i)
            order.push_back({ MakeDrawSortKey(draws[i], resourceManager.GetMaterial(draws[i].Material)), i });

        std::sort(order.begin(), order.end(), [](const DrawSortEntry& a, const DrawSortEntry& b)
        {
            return std::tie(a.Key, a.Index) < std::tie(b.Key, b.Index);
        });
    }
}
