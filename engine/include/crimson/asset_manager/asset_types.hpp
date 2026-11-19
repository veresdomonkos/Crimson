#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_id.hpp"

namespace crimson
{
    struct AssetInfo
    {
        AssetID Id;
        AssetType Type;
        std::string Path; // relative to the asset folder, with forward slashes
        bool ReadOnly = false; // one of the engine's own, built into the executable
        uint32_t ImporterVersion = 1;
        nlohmann::json Settings = nlohmann::json::object();
    };

    // One of the engine's assets that only the renderer can use, like the tone mapping shader or the scene a new one is copied from, which pickers leave out
    [[nodiscard]] inline bool IsInternal(const AssetInfo& info)
    {
        return info.ReadOnly && info.Path.find("/internal/") != std::string::npos;
    }
}
