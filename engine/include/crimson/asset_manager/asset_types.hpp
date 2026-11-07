#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_id.hpp"

namespace crimson
{
    inline std::string_view ToString(AssetType t)
    {
        switch (t)
        {
            case AssetType::Texture:  return "Texture";
            case AssetType::Mesh:     return "Mesh";
            case AssetType::Shader:   return "Shader";
            case AssetType::Material: return "Material";
            case AssetType::Scene:    return "Scene";
            default:                  return "None";
        }
    }

    inline AssetType AssetTypeFromString(std::string_view s)
    {
        if (s == "Texture")  return AssetType::Texture;
        if (s == "Mesh")     return AssetType::Mesh;
        if (s == "Shader")   return AssetType::Shader;
        if (s == "Material") return AssetType::Material;
        if (s == "Scene")    return AssetType::Scene;
        return AssetType::None;
    }

    struct AssetInfo
    {
        AssetID Id;
        AssetType Type = AssetType::None;
        std::string Path; // relative to the asset folder, with forward slashes
        bool ReadOnly = false; // one of the engine's own, built into the executable
        uint32_t ImporterVersion = 1;
        nlohmann::json Settings = nlohmann::json::object();
    };

    // One of the engine's assets that only the renderer can use, like the tone mapping shader, which pickers leave out
    [[nodiscard]] inline bool IsInternal(const AssetInfo& info)
    {
        return info.ReadOnly && (info.Path.starts_with("engine/shaders/internal/") || info.Path.starts_with("engine/materials/internal/"));
    }
}
