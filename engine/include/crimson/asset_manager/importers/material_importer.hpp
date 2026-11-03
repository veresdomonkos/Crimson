#pragma once
#include <string_view>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/material_data.hpp"

namespace crimson
{
    // Sets the settings, properties and textures the data lists on an existing material
    void ApplyMaterialValues(const MaterialData& data, MaterialHandle material, ImportContext& ctx);

    // Sets every property the shader declares a default for
    void ApplyShaderPropertyDefaults(ShaderHandle shader, MaterialHandle material, ImportContext& ctx);

    // The name is what errors report. Errors go to the log and give an invalid handle.
    [[nodiscard]] MaterialHandle CreateMaterialFromData(const MaterialData& data, ImportContext& ctx, std::string_view name);

    class MaterialImporter final : public AssetImporter<AssetType::Material>
    {
    public:
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        std::optional<MaterialHandle> Load(const AssetSource&, ImportContext&) override;
        void Unload(MaterialHandle&, GpuResourceManager&) override;
    };
}
