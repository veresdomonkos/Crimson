#pragma once
#include "crimson/asset_manager/scene_data.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    class AssetManager;

    // What the scene would save as. Instances only keep what can differ between them.
    [[nodiscard]] SceneData CaptureScene(Scene& scene, const SceneEnvironment& environment, const AssetManager& assets);

    // One entity as the scene would save it, with what is under it or not
    [[nodiscard]] SceneNode CaptureNode(Entity entity, Scene& scene, const AssetManager& assets, bool withChildren);

    // Makes the entity what the node says, apart from its children: its name and components, including removing those
    // the node doesn't have
    void ApplyNode(Entity entity, const SceneNode& node, AssetManager& assets);

    // The entity with that id, not counting those inside instances, whose ids are their scene's. Invalid if there is none.
    [[nodiscard]] Entity FindEntity(Scene& scene, AssetID id);

    // Creates the data's entities under the parent, and expands the instances. The owner is the scene being created,
    // if it has a file, so that a scene containing itself stops instead of going on forever.
    void InstantiateScene(const SceneData& data, Scene& scene, Entity parent, AssetManager& assets, SceneID owner);

    // A new instance of a scene, named after its file. Invalid if the source uses the owner, as that would be a loop.
    Entity CreateSceneInstance(SceneID source, Scene& scene, Entity parent, AssetManager& assets, SceneID owner);

    // Whether a scene has an instance of another one, at any depth, or is that one
    [[nodiscard]] bool SceneUses(SceneID scene, SceneID used, const AssetManager& assets);

    // Its source went away, so what came from it goes too. The entity keeps the reference, for the instance to be back
    // when the source is and the scene is opened again.
    void EmptySceneInstance(Entity instance, Scene& scene);
}
