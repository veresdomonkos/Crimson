#pragma once

#include <cstddef>
#include <span>

#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    // Decodes an image file held in memory, for images that don't come from an asset folder.
    // Errors go to the log and give an invalid handle.
    [[nodiscard]] TextureHandle CreateTextureFromMemory(GpuResourceManager& gpu, std::span<const std::byte> file);

    class TextureImporter final : public AssetImporter
    {
    public:
        AssetType Type() const override { return AssetType::Texture; }
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override;

        LoadedAsset Load(const AssetSource&, ImportContext&) override;
        void Unload(const LoadedAsset&, GpuResourceManager&) override;
    };
}