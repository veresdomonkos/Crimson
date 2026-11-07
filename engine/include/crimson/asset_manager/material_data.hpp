#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/renderer/material_settings.hpp"

namespace crimson
{
    class AssetManager;

    // What a .mat file says. It is separate from the GPU's material, so it can be read, edited and written back.
    struct MaterialData
    {
        ShaderID Shader;
        MaterialSettings Settings;
        std::map<std::string, std::vector<float>> Properties; // 1 to 4 values each
        std::map<std::string, TextureID> Textures;

        bool operator==(const MaterialData&) const = default;
    };

    // Missing references leave those fields empty, and only invalid JSON gives nothing. The name is what errors report.
    [[nodiscard]] std::optional<MaterialData> ParseMaterialData(std::string_view text, const AssetManager& assets, std::string_view name);

    // References are written with both id and path, so they survive either one changing
    [[nodiscard]] std::string WriteMaterialData(const MaterialData& data, const AssetManager& assets);

    // Points a .mat file's references to assets that moved (old path to id) at where they are now, and leaves the rest
    // of it as it is. Gives the new text, or nothing if no reference moved.
    [[nodiscard]] std::optional<std::string> UpdateMaterialReferences(std::string_view text, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets);
}
