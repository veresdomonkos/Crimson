#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/scene/component_registry.hpp"
#include "crimson/scene/tone_mapper.hpp"

namespace crimson
{
    class AssetManager;
    struct SceneAsset; // scenes contain scene instances, and a SceneID needs only this declaration

    // Post-processing of what the scene's cameras see
    struct PostProcessSettings
    {
        float Exposure = 2.5f; // GT7 takes 1.0 as 100 nits of SDR white's 250, so this brings 1.0 about to where the old ACES curve had it
        ToneMapper Mapper = ToneMapper::Gt7;

        bool operator==(const PostProcessSettings&) const = default;
    };

    // What a scene saves apart from its entities
    struct SceneSettings
    {
        glm::vec3 Ambient{0.05f};
        AssetID MainCamera; // a node's id, unset means the first active camera (FindMainCamera)
        PostProcessSettings PostProcessing;

        bool operator==(const SceneSettings&) const = default;
    };

    // One entity. An instance only has what can differ between instances, the rest comes from its scene when it is loaded.
    struct SceneNode
    {
        AssetID Id;
        std::string Name;
        bool Active = true;

        // Each under its type's id: the components that are part of the node, like the transform, first, then the others
        // in the order the file has them. One whose type isn't registered is kept as it was.
        std::vector<SceneComponent> Components;
        TypedAssetID<SceneAsset> Instance; // a SceneID
        std::vector<SceneNode> Children;

        [[nodiscard]] const SceneComponent* FindComponent(std::string_view type) const;

        bool operator==(const SceneNode&) const;
    };

    // What a .scene file says. It is separate from the running Scene, as an instance's file can be read without one.
    struct SceneData
    {
        SceneSettings Settings;
        std::vector<SceneNode> Roots;

        bool operator==(const SceneData&) const = default;
    };

    namespace scene_data
    {
        // Invalid JSON, a newer version or a wrongly typed field give nothing, for a broken file not to be saved over.
        // A reference to an asset that isn't there keeps its id, and is written back as it was. The name is what errors report.
        // Every entity needs an id, and AddMissingIds gives them.
        [[nodiscard]] std::optional<SceneData> Parse(std::string_view text, const AssetManager& assets, std::string_view name);

        [[nodiscard]] std::string Write(const SceneData& data, const AssetManager& assets);

        // Points a .scene file's references to assets that moved (old path to id) at where they are now, and leaves the rest
        // of it as it is. Gives the new text, or nothing if no reference moved.
        [[nodiscard]] std::optional<std::string> UpdateReferences(std::string_view text, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets);

        // The text with an id given to every entity that has none (an invalid one stays, for Parse to reject).
        // Nothing if the text isn't a scene object, or every entity has one.
        [[nodiscard]] std::optional<std::string> AddMissingIds(std::string_view text);

        // An id of its own for every entity, with the main camera following its entity
        void GiveNewIds(SceneData& data);
    }
}
