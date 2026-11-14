#include "crimson/scene/scene_serializer.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    namespace
    {
        void ApplyComponents(Entity entity, const std::vector<SceneComponent>& components, AssetManager& assets)
        {
            for (const SceneComponent& component : components)
            {
                const ComponentType* type = assets.Components().Find(component.Type);

                // Kept as it was read, to be saved again
                if (!type || !component.Raw.empty())
                {
                    entity.GetScene()->GetRegistry().get_or_emplace<UnknownComponents>(entity.GetHandle()).Components.push_back(component);
                    continue;
                }

                WriteComponent(*type, entity, component.Values);
                ResolveComponentAssets(*type, type->Get(entity), assets);
            }
        }

        // Instantiating one scene, with the chain of scenes it is inside of
        struct Instantiation
        {
            Scene& Target;
            AssetManager& Assets;
            std::vector<SceneID> Chain;

            void CreateNode(const SceneNode& node, Entity parent, bool readOnly)
            {
                const Entity entity = Target.CreateEntity(node.Name, parent);

                auto& name = entity.GetComponent<NameComponent>();
                name.Id = node.Id;
                name.Active = node.Active;

                if (readOnly)
                    Target.GetRegistry().emplace<InstancedComponent>(entity.GetHandle()); // empty, so AddComponent has no reference to give

                ApplyComponents(entity, node.Components, Assets);

                if (node.Instance)
                    Expand(entity, node.Instance);

                for (const SceneNode& child : node.Children)
                    CreateNode(child, entity, readOnly);
            }

            void Expand(Entity instance, SceneID source)
            {
                instance.AddComponent<SceneInstanceComponent>(source);

                if (std::ranges::find(Chain, source) != Chain.end())
                {
                    const AssetInfo* info = Assets.GetInfo(source);
                    LOG_ERROR("[Scene] {} contains itself, so that instance stays empty", info ? info->Path : "A scene");
                    return;
                }

                const std::optional<SceneData> data = Assets.Read(source);
                if (!data)
                    return;

                Chain.push_back(source);

                for (const SceneNode& root : data->Roots)
                    CreateNode(root, instance, true);

                Chain.pop_back();
            }
        };

        void CollectInstances(const std::vector<SceneNode>& nodes, std::vector<SceneID>& instances)
        {
            for (const SceneNode& node : nodes)
            {
                if (node.Instance)
                    instances.push_back(node.Instance);

                CollectInstances(node.Children, instances);
            }
        }
    }

    SceneNode CaptureNode(Entity entity, Scene& scene, const AssetManager& assets, bool withChildren)
    {
        const auto& name = entity.GetComponent<NameComponent>();

        SceneNode node;
        node.Id = name.Id;
        node.Name = name.Name;
        node.Active = name.Active;

        // An instance only has what can differ between instances, the rest comes from its scene
        const auto* instance = entity.TryGetComponent<SceneInstanceComponent>();

        for (const ComponentType& type : assets.Components().Types())
        {
            if (type.Has(entity) && (!instance || type.InlineInFile))
                node.Components.push_back(CaptureComponent(type, entity, assets));
        }

        if (instance)
        {
            node.Instance = instance->Scene;
            return node;
        }

        if (const auto* unknown = entity.TryGetComponent<UnknownComponents>())
            node.Components.insert(node.Components.end(), unknown->Components.begin(), unknown->Components.end());

        if (withChildren)
        {
            for (const entt::entity child : entity.GetComponent<RelationshipComponent>().Children)
                node.Children.push_back(CaptureNode(scene.GetEntity(child), scene, assets, true));
        }

        return node;
    }

    SceneData CaptureScene(Scene& scene, const SceneEnvironment& environment, const AssetManager& assets)
    {
        SceneData data;
        data.Environment = environment;

        for (const entt::entity root : scene.GetRoots())
            data.Roots.push_back(CaptureNode(scene.GetEntity(root), scene, assets, true));

        return data;
    }

    void ApplyNode(Entity entity, const SceneNode& node, AssetManager& assets)
    {
        auto& name = entity.GetComponent<NameComponent>();
        name.Name = node.Name;
        name.Active = node.Active;

        for (const ComponentType& type : assets.Components().Types())
        {
            if (!type.InlineInFile && type.Has(entity) && !node.FindComponent(type.Id))
                type.Remove(entity);
        }

        entity.RemoveComponent<UnknownComponents>();
        ApplyComponents(entity, node.Components, assets);
    }

    Entity FindEntity(Scene& scene, AssetID id)
    {
        for (const auto [handle, name] : scene.GetRegistry().view<NameComponent>(entt::exclude<InstancedComponent>).each())
        {
            if (name.Id == id)
                return scene.GetEntity(handle);
        }

        return {};
    }

    void InstantiateScene(const SceneData& data, Scene& scene, Entity parent, AssetManager& assets, SceneID owner)
    {
        Instantiation instantiation{ scene, assets, {} };

        if (owner)
            instantiation.Chain.push_back(owner);

        for (const SceneNode& root : data.Roots)
            instantiation.CreateNode(root, parent, false);
    }

    Entity CreateSceneInstance(SceneID source, Scene& scene, Entity parent, AssetManager& assets, SceneID owner)
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

    bool SceneUses(SceneID scene, SceneID used, const AssetManager& assets)
    {
        std::vector<SceneID> visited;
        std::vector<SceneID> pending = { scene };

        while (!pending.empty())
        {
            const SceneID current = pending.back();
            pending.pop_back();

            if (current == used)
                return true;

            if (std::ranges::find(visited, current) != visited.end())
                continue;

            visited.push_back(current);

            if (const std::optional<SceneData> data = assets.Read(current))
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
