#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/asset_manager/importers/material_importer.hpp"
#include "crimson/asset_manager/importers/mesh_importer.hpp"
#include "crimson/asset_manager/importers/scene_importer.hpp"
#include "crimson/asset_manager/importers/shader_importer.hpp"
#include "crimson/asset_manager/importers/texture_importer.hpp"

namespace crimson
{
    // The one place that names every type of asset the engine has. Each comes after the ones it uses.
    void RegisterBuiltinAssetTypes(AssetManager& assets)
    {
        assets.RegisterType<TextureAsset, TextureImporter>();
        assets.RegisterType<ShaderAsset, ShaderImporter>();
        assets.RegisterType<MeshAsset, MeshImporter>();
        assets.RegisterType<MaterialAsset, MaterialImporter>();
        assets.RegisterType<SceneAsset, SceneImporter>();
    }
}
