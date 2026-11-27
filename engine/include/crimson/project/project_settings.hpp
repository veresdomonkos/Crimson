#pragma once
#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/asset_manager/importers/scene_asset.hpp"

namespace crimson
{
    // What project.crimson holds besides the project's identity, read once the assets are there, shared by everyone on the project
    struct ProjectSettings
    {
        // The scene a built game starts with. It is not the one the editor opens, which is per user.
        SceneID MainScene;

        bool operator==(const ProjectSettings&) const = default;
    };

    namespace project_settings
    {
        // Each reference is read like one in an asset file, so a missing one is kept as a missing id. It never throws,
        // and a section that isn't an object reads as defaults.
        [[nodiscard]] ProjectSettings Read(const nlohmann::ordered_json& section, const AssetManager& assets);

        // An empty setting is left out, and a missing one is written back as it was read
        [[nodiscard]] nlohmann::ordered_json Write(const ProjectSettings& settings, const AssetManager& assets);
    }
}
