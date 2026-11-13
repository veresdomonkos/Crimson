#include "crimson/asset_manager/importers/scene_importer.hpp"

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    static constexpr std::string_view kNewSceneTemplate = "engine/scenes/internal/SC_NewScene.scene";

    std::span<const std::string_view> SceneImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".scene" };
        return exts;
    }

    std::optional<std::string> SceneImporter::NewFile(const AssetManager& assets) const
    {
        // Read and written again, as parsing gives every node an id of its own, for two new scenes not to share any
        const std::optional<SceneData> data = assets.Read(assets.Find<AssetType::Scene>(kNewSceneTemplate));
        if (!data)
        {
            LOG_ERROR("[SceneImporter] Can't read {}", kNewSceneTemplate);
            return std::nullopt;
        }

        return WriteSceneData(*data, assets);
    }

    std::optional<std::string> SceneImporter::UpdateReferences(const AssetSource& source, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets) const
    {
        const std::optional<std::string> text = source.Read();
        return text ? UpdateSceneReferences(*text, moved, assets) : std::nullopt;
    }
}
