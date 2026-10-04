#pragma once
#include <crimson/asset_manager/asset_importer.hpp>

namespace crimson::editor
{
    class ShaderImporter final : public AssetImporter
    {
    public:
        AssetType Type() const override { return AssetType::Shader; }
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        LoadedAsset Load(const AssetMetadata&, const std::filesystem::path&, ImportContext&) override;
        void Unload(const LoadedAsset&, GpuResourceManager&) override;
    };
}
