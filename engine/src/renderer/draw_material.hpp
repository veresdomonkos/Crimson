#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <unordered_map>

#include "crimson/renderer/frame_recorder.hpp"
#include "crimson/renderer/material_properties.hpp"
#include "crimson/renderer/property_id.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/shader_interface.hpp"

// What a draw's MaterialProperties change of its material, written once for both backends. A backend only allocates, writes
// and binds. A name the shader doesn't have, or a value larger than its property, is skipped without a message, as one per
// draw would repeat every frame.
namespace crimson
{
    // The textures a material has set, by the id of their slot. A slot that isn't in it shows the backend's default texture.
    using MaterialTextures = std::unordered_map<PropertyId, TextureHandle, PropertyIdHash>;

    // What a frame's draws with properties need, counted before any is recorded, so a backend sizes its pool and buffer once
    struct MaterialPropertiesBudget
    {
        uint32_t Draws = 0;
        uint32_t Textures = 0;
        std::size_t BlockBytes = 0;
    };

    // The property of the shader that a value entry writes, or none for a texture entry, a name the shader does not have or a
    // value larger than its property
    [[nodiscard]] inline const ShaderPropertyInfo* FindWritableProperty(const MaterialPropertyEntry& entry, const ShaderInterface& shader)
    {
        if (entry.Size == 0)
            return nullptr;

        const auto property = shader.Properties.find(entry.Id);
        return property != shader.Properties.end() && entry.Size <= property->second.Size ? &property->second : nullptr;
    }

    // Whether the properties write a value into the shader's block. When they don't, the draw reads its material's own block.
    [[nodiscard]] inline bool SetsValues(std::span<const MaterialPropertyEntry> entries, const ShaderInterface& shader)
    {
        if (shader.MaterialBlockSize == 0)
            return false;

        for (const MaterialPropertyEntry& entry : entries)
        {
            if (FindWritableProperty(entry, shader))
                return true;
        }

        return false;
    }

    // Copies the material's block, then writes each value of the entries over the property it names. The block and the
    // material's bytes are the shader's MaterialBlockSize.
    inline void WriteMaterialBlock(std::span<std::byte> block, std::span<const std::byte> material, const ShaderInterface& shader,
                                   std::span<const MaterialPropertyEntry> entries, std::span<const std::byte> values)
    {
        assert(block.size() == material.size());
        std::memcpy(block.data(), material.data(), std::min(block.size(), material.size()));

        for (const MaterialPropertyEntry& entry : entries)
        {
            if (const ShaderPropertyInfo* property = FindWritableProperty(entry, shader))
                std::memcpy(block.data() + property->Offset, values.data() + entry.Offset, entry.Size);
        }
    }

    // The texture a draw has in a slot: the one its properties set, else the material's, else none, which the backend's default
    // texture stands in for
    [[nodiscard]] inline TextureHandle DrawTexture(PropertyId slot, const MaterialTextures& material, std::span<const MaterialPropertyEntry> entries)
    {
        for (const MaterialPropertyEntry& entry : entries)
        {
            if (entry.Size == 0 && entry.Id == slot)
                return entry.Texture;
        }

        const auto found = material.find(slot);
        return found != material.end() ? found->second : TextureHandle{};
    }

    // Counts the draws of every pass that have properties, the texture slots of their shaders, and the block bytes of those
    // that write values, each block rounded up to blockAlignment. ResourceManager gives GetMaterial(handle).Shader and
    // GetShaderInterface(shader), like the resource managers of the backends.
    template <typename ResourceManager>
    MaterialPropertiesBudget CountMaterialProperties(const FramePacket& packet, const ResourceManager& resources, std::size_t blockAlignment)
    {
        blockAlignment = std::max<std::size_t>(blockAlignment, 1);
        MaterialPropertiesBudget budget;

        for (const PassRecord& pass : packet.GetPasses())
        {
            for (const DrawItem& draw : pass.GetDraws())
            {
                if (!draw.Properties)
                    continue;

                const ShaderInterface& shader = resources.GetShaderInterface(resources.GetMaterial(draw.Material).Shader);

                ++budget.Draws;
                budget.Textures += static_cast<uint32_t>(shader.Textures.size());

                if (SetsValues(packet.GetPropertyEntries(draw.Properties), shader))
                    budget.BlockBytes += (shader.MaterialBlockSize + blockAlignment - 1) / blockAlignment * blockAlignment;
            }
        }

        return budget;
    }
}
