#pragma once
#include "crimson/asset_manager/scene_data.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    class AssetManager;

    // What the scene would save as. Instances only keep what can differ between them.
    [[nodiscard]] SceneData CaptureScene(Scene& scene, const SceneEnvironment& environment, const AssetManager& assets);

    // Creates the data's entities under the parent, and expands the instances. The owner is the scene being created,
    // if it has a file, so that a scene containing itself stops instead of going on forever.
    void InstantiateScene(const SceneData& data, Scene& scene, Entity parent, AssetManager& assets, AssetID owner);

    // A new instance of a scene, named after its file. Invalid if the source uses the owner, as that would be a loop.
    Entity CreateSceneInstance(AssetID source, Scene& scene, Entity parent, AssetManager& assets, AssetID owner);

    // Whether a scene has an instance of another one, at any depth, or is that one
    [[nodiscard]] bool SceneUses(AssetID scene, AssetID used, const AssetManager& assets);

    // Its source went away, so the entity stays as an empty one
    void BreakSceneInstance(Entity instance, Scene& scene);
}
