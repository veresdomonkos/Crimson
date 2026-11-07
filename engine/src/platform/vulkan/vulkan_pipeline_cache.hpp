#pragma once

#include <unordered_map>
#include <functional>

#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/material_settings.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/shader_pass.hpp"

namespace crimson::vulkan
{
    struct GraphicsPipelineInfo
    {
        BufferLayout Layout;
        ShaderHandle Shader;
        RenderTargetHandle Target;
        MaterialSettings Settings;
        ShaderPass Pass = ShaderPass::Main;

        bool operator==(const GraphicsPipelineInfo& other) const
        {
            return Layout == other.Layout && Shader == other.Shader && Target == other.Target && Settings == other.Settings && Pass == other.Pass;
        }
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

            HashCombine(hash, key.Shader.GetRaw());
            HashCombine(hash, key.Target.GetRaw());
            HashCombine(hash, static_cast<size_t>(key.Settings.RenderMode));
            HashCombine(hash, static_cast<size_t>(key.Settings.Cull));
            HashCombine(hash, static_cast<size_t>(key.Pass));

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
