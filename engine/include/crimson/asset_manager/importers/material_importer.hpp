#pragma once
#include <string_view>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/material_data.hpp"

namespace crimson
{
    // Sets the settings, properties and textures the data lists on an existing material
    void ApplyMaterialValues(const MaterialData& data, MaterialHandle material, ImportContext& ctx);

    // The name is what errors report. Errors go to the log and give an invalid handle.
    [[nodiscard]] MaterialHandle CreateMaterialFromData(const MaterialData& data, ImportContext& ctx, std::string_view name);

    class MaterialImporter final : public AssetImporter
    {
    public:
        AssetType Type() const override { return AssetType::Material; }
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        LoadedAsset Load(const AssetSource&, ImportContext&) override;
        void Unload(const LoadedAsset&, GpuResourceManager&) override;
    };
}
