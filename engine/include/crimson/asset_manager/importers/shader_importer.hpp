#pragma once
#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    class ShaderImporter final : public AssetImporter<AssetType::Shader>
    {
    public:
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;
        std::optional<std::string> NewFile(const AssetManager&) const override; // a simple lit one, to start from
        bool ReloadsWhenChanged() const override { return true; }

        std::optional<ShaderHandle> Load(const AssetSource&, ImportContext&) override;
        void Unload(ShaderHandle&, GpuResourceManager&) override;
    };
}
