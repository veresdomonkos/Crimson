#pragma once
#include <filesystem>
#include <string_view>
#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_id.hpp"

namespace crimson
{
    enum class AssetType : uint8_t { None, Texture, Mesh, Shader, Material };

    inline std::string_view ToString(AssetType t)
    {
        switch (t)
        {
            case AssetType::Texture:  return "Texture";
            case AssetType::Mesh:     return "Mesh";
            case AssetType::Shader:   return "Shader";
            case AssetType::Material: return "Material";
            default:                  return "None";
        }
    }

    inline AssetType AssetTypeFromString(std::string_view s)
    {
        if (s == "Texture")  return AssetType::Texture;
        if (s == "Mesh")     return AssetType::Mesh;
        if (s == "Shader")   return AssetType::Shader;
        if (s == "Material") return AssetType::Material;
        return AssetType::None;
    }

    struct AssetMetadata
    {
        AssetID Id;
        AssetType Type = AssetType::None;
        std::filesystem::path Source;
        uint32_t ImporterVersion = 1;
        nlohmann::json Settings = nlohmann::json::object();
    };
}