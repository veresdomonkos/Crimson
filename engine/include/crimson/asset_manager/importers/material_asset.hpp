#pragma once
#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/asset_manager/material_data.hpp"
#include "crimson/renderer/resource_handles.hpp"

namespace crimson
{
    // A .mat file, loaded as a material. Its file says what it is made from.
    struct MaterialAsset
    {
        using Loaded = MaterialHandle;
        using Result = MaterialHandle;
        using Data = MaterialData;

        static constexpr AssetType Tag = AssetType::Material;

        static constexpr auto Parse = &material_data::Parse;
        static constexpr auto Write = &material_data::Write;
    };

    using MaterialID = TypedAssetID<MaterialAsset>;
}
