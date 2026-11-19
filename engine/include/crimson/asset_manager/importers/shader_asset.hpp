#pragma once
#include <array>
#include <string_view>

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

        static constexpr std::string_view Name = "Shader"; // as files have it
        static constexpr std::array<AssetType, 0> Uses{};
    };

    using ShaderID = TypedAssetID<ShaderAsset>;
}
