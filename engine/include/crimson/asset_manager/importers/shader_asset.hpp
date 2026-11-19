#pragma once
#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/renderer/resource_handles.hpp"

namespace crimson
{
    // A shader's source, compiled and loaded
    struct ShaderAsset
    {
        using Loaded = ShaderHandle;
        using Result = ShaderHandle;
        using Data = NoAssetData;

        static constexpr AssetType Tag = AssetType::Shader;
    };

    using ShaderID = TypedAssetID<ShaderAsset>;
}
