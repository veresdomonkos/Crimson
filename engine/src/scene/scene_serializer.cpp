#include "crimson/scene/scene_serializer.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"
#include "crimson/scene/scene_renderer.hpp"

namespace crimson
{
    namespace
    {
        SceneNode CaptureNode(Entity entity, Scene& scene, const AssetManager& assets)
        {
            const auto& name = entity.GetComponent<NameComponent>();
            const auto& transform = entity.GetComponent<TransformComponent>();

            SceneNode node;
            node.Id = name.Id;
            node.Name = name.Name;
            node.Active = name.Active;
            node.Position = transform.Position;
            node.RotationDegrees = transform.RotationDegrees;
            node.Scale = transform.Scale;

            if (const auto* instance = entity.TryGetComponent<SceneInstanceComponent>())
            {
                node.Instance = instance->Scene;
                return node;
            }

            if (const auto* light = entity.TryGetComponent<LightComponent>())
                node.Light = *light;

            if (const auto* renderer = entity.TryGetComponent<MeshRendererComponent>())
            {
                SceneMeshRenderer& mesh = node.MeshRenderer.emplace();
                mesh.Mesh = renderer->MeshAsset;
                mesh.CastsShadows = renderer->CastsShadows;

                // The default is what an empty slot ends up with, and left empty it can still find the material made for it
                for (const AssetID material : renderer->MaterialAssets)
                    mesh.Materials.push_back(material == assets.DefaultMaterial() ? AssetID{} : material);
            }

            for (const entt::entity child : entity.GetComponent<RelationshipComponent>().Children)
                node.Children.push_back(CaptureNode(scene.GetEntity(child), scene, assets));

            return node;
        }

        // Instantiating one scene, with the chain of scenes it is inside of
        struct Instantiation
        {
            Scene& Target;
            AssetManager& Assets;
            std::vector<AssetID> Chain;

            void CreateNode(const SceneNode& node, Entity parent, bool readOnly)
            {
                const Entity entity = Target.CreateEntity(node.Name, parent);

                auto& name = entity.GetComponent<NameComponent>();
                name.Id = node.Id;
                name.Active = node.Active;

                auto& transform = entity.GetComponent<TransformComponent>();
                transform.Position = node.Position;
                transform.RotationDegrees = node.RotationDegrees;
                transform.Scale = node.Scale;

                if (readOnly)
                    Target.GetRegistry().emplace<InstancedComponent>(entity.GetHandle()); // empty, so AddComponent has no reference to give

                if (node.Light)
                    entity.AddComponent<LightComponent>(*node.Light);

                if (node.MeshRenderer)
                {
                    auto& renderer = entity.AddComponent<MeshRendererComponent>();
                    renderer.MeshAsset = node.MeshRenderer->Mesh;
                    renderer.MaterialAssets = node.MeshRenderer->Materials;
                    renderer.CastsShadows = node.MeshRenderer->CastsShadows;
                    ResolveMeshRenderer(renderer, Assets);
                }

                if (node.Instance)
                    Expand(entity, node.Instance);

                for (const SceneNode& child : node.Children)
                    CreateNode(child, entity, readOnly);
            }

            void Expand(Entity instance, AssetID source)
            {
                instance.AddComponent<SceneInstanceComponent>(source);

                if (std::ranges::find(Chain, source) != Chain.end())
                {
                    const AssetInfo* info = Assets.GetInfo(source);
                    LOG_ERROR("[Scene] {} contains itself, so that instance stays empty", info ? info->Path : "A scene");
                    return;
                }

                const std::optional<SceneData> data = Assets.ReadSceneFile(source);
                if (!data)
                    return;

                Chain.push_back(source);

                for (const SceneNode& root : data->Roots)
                    CreateNode(root, instance, true);

                Chain.pop_back();
            }
        };

        void CollectInstances(const std::vector<SceneNode>& nodes, std::vector<AssetID>& instances)
        {
            for (const SceneNode& node : nodes)
            {
                if (node.Instance)
                    instances.push_back(node.Instance);

                CollectInstances(node.Children, instances);
            }
        }
    }

    SceneData CaptureScene(Scene& scene, const SceneEnvironment& environment, const AssetManager& assets)
    {
        SceneData data;
        data.Environment = environment;

        for (const entt::entity root : scene.GetRoots())
            data.Roots.push_back(CaptureNode(scene.GetEntity(root), scene, assets));

        return data;
    }

    void InstantiateScene(const SceneData& data, Scene& scene, Entity parent, AssetManager& assets, AssetID owner)
    {
        Instantiation instantiation{ scene, assets, {} };

        if (owner)
            instantiation.Chain.push_back(owner);

        for (const SceneNode& root : data.Roots)
            instantiation.CreateNode(root, parent, false);
    }

    Entity CreateSceneInstance(AssetID source, Scene& scene, Entity parent, AssetManager& assets, AssetID owner)
    {
        const AssetInfo* info = assets.GetInfo(source);
        if (!info || info->Type != AssetType::Scene)
            return {};

        if (owner && SceneUses(source, owner, assets))
        {
            LOG_ERROR("[Scene] Can't put {} in here, as it would then contain itself", info->Path);
            return {};
        }

        const Entity entity = scene.CreateEntity(std::filesystem::path(info->Path).stem().string(), parent);

        Instantiation instantiation{ scene, assets, {} };

        if (owner)
            instantiation.Chain.push_back(owner);

        instantiation.Expand(entity, source);
        return entity;
    }

    bool SceneUses(AssetID scene, AssetID used, const AssetManager& assets)
    {
        std::vector<AssetID> visited;
        std::vector<AssetID> pending = { scene };

        while (!pending.empty())
        {
            const AssetID current = pending.back();
            pending.pop_back();

            if (current == used)
                return true;

            if (std::ranges::find(visited, current) != visited.end())
                continue;

            visited.push_back(current);

            if (const std::optional<SceneData> data = assets.ReadSceneFile(current))
                CollectInstances(data->Roots, pending);
        }

        return false;
    }

    void EmptySceneInstance(Entity instance, Scene& scene)
    {
        // Each child removes itself from this list, so it needs a copy
        const std::vector<entt::entity> children = instance.GetComponent<RelationshipComponent>().Children;
        for (const entt::entity child : children)
            scene.DestroyEntity(scene.GetEntity(child));
    }
}
