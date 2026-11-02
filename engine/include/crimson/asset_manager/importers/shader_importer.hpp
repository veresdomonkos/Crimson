#pragma once
#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    class ShaderImporter final : public AssetImporter
    {
    public:
        AssetType Type() const override { return AssetType::Shader; }
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        LoadedAsset Load(const AssetSource&, ImportContext&) override;
        void Unload(const LoadedAsset&, GpuResourceManager&) override;
    };
}
