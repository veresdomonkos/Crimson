#pragma once
#include <array>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/asset_manager/scene_data.hpp"

namespace crimson
{
    // A .scene file. It loads as nothing: it is read as data instead.
    struct SceneAsset
    {
        using Data = SceneData;

        static constexpr AssetType Tag = AssetType::Scene;
        static constexpr std::array<AssetType, 0> Uses{}; // a scene loads nothing, so it holds nothing

        static constexpr auto Parse = &scene_data::Parse;
        static constexpr auto Write = &scene_data::Write;
    };

    using SceneID = TypedAssetID<SceneAsset>;
}
