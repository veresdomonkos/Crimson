#pragma once
#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    // Only makes .scene files known as assets. A scene has nothing on the GPU, so AssetManager::Read gives its data instead.
    class SceneImporter final : public AssetImporterBase
    {
    public:
        AssetType Type() const override { return AssetType::Scene; }
        std::span<const std::string_view> Extensions() const override;
        nlohmann::json DefaultSettings() const override { return nlohmann::json::object(); }

        // A copy of the engine's template scene
        std::optional<std::string> NewFile(const AssetManager&) const override;
        std::optional<std::string> UpdateReferences(const AssetSource& source, const std::unordered_map<std::string, AssetID>& moved, const AssetManager&) const override;
    };
}
