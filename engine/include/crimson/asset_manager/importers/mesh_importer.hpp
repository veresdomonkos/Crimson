#pragma once
#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    class MeshImporter final : public AssetImporter<AssetType::Mesh>
    {
    public:
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        std::optional<Mesh> Load(const AssetSource&, ImportContext&) override;
        void Unload(Mesh&, GpuResourceManager&) override;
    };
}