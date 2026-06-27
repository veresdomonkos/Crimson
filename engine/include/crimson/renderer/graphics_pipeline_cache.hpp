#pragma once
#include "crimson/renderer/buffer_layout.hpp"

namespace crimson
{
    struct GraphicsPipelineInfo
    {
        BufferLayout Layout;

        bool operator==(const GraphicsPipelineInfo &) const = default;
    };

    struct GraphicsPipelineInfoHash
    {
        size_t operator()(const GraphicsPipelineInfo& key) const
        {
            size_t hash = 0;

            for (const auto& element : key.Layout)
            {
                HashCombine(
                    hash,
                    std::hash<size_t>{}(
                        static_cast<size_t>(element.Type)
                    )
                );
            }

            //HashCombine(hash, key.Layout.GetStride());

            return hash;
        }
    private:
        static void HashCombine(size_t& seed, size_t value)
        {
            seed ^= value
                + 0x9e3779b9
                + (seed << 6)
                + (seed >> 2);
        }
    };

    template <typename T>
    using GraphicsPipelineCache = std::unordered_map<GraphicsPipelineInfo, T, GraphicsPipelineInfoHash>;
}
