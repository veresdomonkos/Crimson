#pragma once
#include <array>
#include <string_view>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/renderer/resource_handles.hpp"

namespace crimson
{
    // An image, loaded as a texture
    struct TextureAsset
    {
        using Loaded = TextureHandle;
        using Result = TextureHandle;
        using Data = NoAssetData;

        static constexpr std::string_view Name = "Texture"; // as files have it
        static constexpr std::array<AssetType, 0> Uses{};
    };

    using TextureID = TypedAssetID<TextureAsset>;
}
