#pragma once
#include <array>
#include <string_view>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/asset_manager/importers/shader_asset.hpp"
#include "crimson/asset_manager/importers/texture_asset.hpp"
#include "crimson/asset_manager/material_data.hpp"
#include "crimson/renderer/resource_handles.hpp"

namespace crimson
{
    // A .mat file, loaded as a material. Its file says what it is made from.
    struct MaterialAsset
    {
        using Loaded = MaterialHandle;
        using Data = MaterialData;

        static constexpr std::string_view Name = "Material"; // as files have it
        static constexpr std::array<AssetType, 2> Uses = { AssetType::Of<ShaderAsset>(), AssetType::Of<TextureAsset>() }; // the types it is made from, unloaded after it

        static constexpr auto Parse = &material_data::Parse;
        static constexpr auto Write = &material_data::Write;
    };

    using MaterialID = TypedAssetID<MaterialAsset>;
}
