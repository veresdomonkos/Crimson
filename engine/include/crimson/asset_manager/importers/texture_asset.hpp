#pragma once
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

        static constexpr AssetType Tag = AssetType::Texture;
    };

    using TextureID = TypedAssetID<TextureAsset>;
}
