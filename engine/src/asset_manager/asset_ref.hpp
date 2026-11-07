#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_id.hpp"

namespace crimson
{
    class AssetManager;

    // How the JSON asset formats point to other assets: { "guid": ..., "path": ... }, or only a path when written by hand.
    // The id goes first, and the path is what's left if the id went away. One whose asset isn't there keeps its id
    // (made up from the path if it had none), and AssetManager remembers how it was written, for WriteAssetRef.
    [[nodiscard]] AssetID ResolveAssetRef(const AssetManager& assets, const nlohmann::json& ref, std::string_view context);

    [[nodiscard]] nlohmann::ordered_json WriteAssetRef(const AssetManager& assets, AssetID id);

    // Points a reference to an asset that moved (old path to id) at where it is now. Returns whether it changed.
    bool UpdateAssetRef(nlohmann::ordered_json& ref, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets);
}
