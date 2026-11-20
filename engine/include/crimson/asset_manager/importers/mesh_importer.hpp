#pragma once
#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/importers/mesh_asset.hpp"

namespace crimson
{
    class MeshImporter final : public AssetImporter<MeshAsset>
    {
    public:
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        std::optional<Mesh> Load(const AssetSource&, ImportContext&) override;
        void Unload(Mesh&, GpuResourceManager&) override;
    };
}
