#include "crimson/project/project_settings.hpp"

#include "asset_manager/asset_ref.hpp"

namespace crimson::project_settings
{
    // A new setting is one member of ProjectSettings, and a line here and in Write
    ProjectSettings Read(const nlohmann::ordered_json& section, const AssetManager& assets)
    {
        ProjectSettings settings;

        if (!section.is_object())
            return settings;

        if (const auto scene = section.find("mainScene"); scene != section.end())
            settings.MainScene = ReadAssetRef<SceneAsset>(assets, nlohmann::json(*scene), "project.crimson main scene");

        return settings;
    }

    nlohmann::ordered_json Write(const ProjectSettings& settings, const AssetManager& assets)
    {
        nlohmann::ordered_json section = nlohmann::ordered_json::object();

        if (settings.MainScene)
            section["mainScene"] = WriteAssetRef(assets, settings.MainScene);

        return section;
    }
}
