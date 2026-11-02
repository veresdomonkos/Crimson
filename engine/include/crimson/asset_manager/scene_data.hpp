#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/scene/components.hpp"
#include "crimson/scene/tone_mapper.hpp"

namespace crimson
{
    class AssetManager;

    // How a scene is lit and shown, apart from its lights
    struct SceneEnvironment
    {
        glm::vec3 Ambient{0.05f};
        float Exposure = 1.0f;
        ToneMapper Mapper = ToneMapper::Aces;

        bool operator==(const SceneEnvironment&) const = default;
    };

    struct SceneMeshRenderer
    {
        AssetID Mesh;
        std::vector<AssetID> Materials; // an empty slot fills itself when the scene is loaded
        bool CastsShadows = true;

        bool operator==(const SceneMeshRenderer&) const = default;
    };

    // One entity. An instance only has what can differ between instances, the rest comes from its scene when it is loaded.
    struct SceneNode
    {
        AssetID Id;
        std::string Name;
        bool Active = true;
        glm::vec3 Position{0.0f};
        glm::vec3 RotationDegrees{0.0f};
        glm::vec3 Scale{1.0f};

        std::optional<LightComponent> Light;
        std::optional<SceneMeshRenderer> MeshRenderer;
        AssetID Instance;
        std::vector<SceneNode> Children;

        bool operator==(const SceneNode&) const;
    };

    // What a .scene file says. It is separate from the running Scene, as an instance's file can be read without one.
    struct SceneData
    {
        SceneEnvironment Environment;
        std::vector<SceneNode> Roots;

        bool operator==(const SceneData&) const = default;
    };

    // Invalid JSON, a newer version or a wrongly typed field give nothing, for a broken file not to be saved over.
    // A missing reference only leaves that field empty. The name is what errors report.
    [[nodiscard]] std::optional<SceneData> ParseSceneData(std::string_view text, const AssetManager& assets, std::string_view name);

    [[nodiscard]] std::string WriteSceneData(const SceneData& data, const AssetManager& assets);

    // Points a .scene file's references to assets that moved (old path to id) at where they are now, and leaves the rest
    // of it as it is. Gives the new text, or nothing if no reference moved.
    [[nodiscard]] std::optional<std::string> UpdateSceneReferences(std::string_view text, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets);
}
