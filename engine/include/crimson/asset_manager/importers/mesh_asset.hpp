#pragma once
#include <array>
#include <string_view>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/renderer/mesh.hpp"

namespace crimson
{
    // A model file, loaded as a mesh
    struct MeshAsset
    {
        using Loaded = Mesh;
        using Result = const Mesh*; // valid as long as the manager
        using Data = NoAssetData;

        static constexpr std::string_view Name = "Mesh"; // as files have it
        static constexpr std::array<AssetType, 0> Uses{};
    };

    using MeshID = TypedAssetID<MeshAsset>;
}
