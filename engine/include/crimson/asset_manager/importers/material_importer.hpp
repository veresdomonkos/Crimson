#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/material_data.hpp"

namespace crimson
{
    // Sets the settings, properties and textures the data lists on an existing material
    void ApplyMaterialValues(const MaterialData& data, MaterialHandle material, ImportContext& ctx);

    // Sets one property, of 1 to 4 floats
    void ApplyMaterialProperty(GpuResourceManager& gpu, MaterialHandle material, const std::string& name, const std::vector<float>& values);

    // Sets every property the shader declares a default for
    void ApplyShaderPropertyDefaults(ShaderHandle shader, MaterialHandle material, ImportContext& ctx);

    class MaterialImporter final : public AssetImporter<AssetType::Material>
    {
    public:
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;
        std::optional<std::string> NewFile(const AssetManager&) const override;
        std::optional<std::string> UpdateReferences(const AssetSource& source, const std::unordered_map<std::string, AssetID>& moved, const AssetManager&) const override;
        void ReleaseShared(GpuResourceManager&) override;

        std::optional<MaterialHandle> Load(const AssetSource&, ImportContext&) override;
        void Unload(MaterialHandle&, GpuResourceManager&) override;
        std::optional<MaterialHandle> Create(const MaterialData&, const AssetSource&, ImportContext&) override;
        bool Update(MaterialHandle&, const MaterialData& from, const MaterialData& to, ImportContext&) override;
        bool UpdateUse(MaterialHandle&, const AssetInfo&, AssetID used, ImportContext&) override;
    private:
        // The name is what errors report. Errors go to the log and give an invalid handle.
        [[nodiscard]] MaterialHandle CreateMaterialFromData(const MaterialData& data, ImportContext& ctx, std::string_view name);

        // What a material's texture slot uses when nothing is assigned to it. Invalid means the backend's white texture.
        [[nodiscard]] TextureHandle EmptySlotTexture(ImportContext& ctx, ShaderHandle shader, const std::string& slot);

        TextureHandle m_flatNormal; // made when a shader first needs it
    };
}
