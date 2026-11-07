#pragma once
#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    // Only makes .scene files known as assets. A scene has nothing on the GPU, and AssetManager::ReadSceneFile reads it.
    class SceneImporter final : public AssetImporterBase
    {
    public:
        AssetType Type() const override { return AssetType::Scene; }
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override { return nlohmann::json::object(); }
    };
}
