#include "crimson_editor/scene_history.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_serializer.hpp>

namespace crimson::editor
{
    namespace
    {
        void CollectIds(const SceneNode& node, std::vector<AssetID>& ids)
        {
            ids.push_back(node.Id);
            for (const SceneNode& child : node.Children)
                CollectIds(child, ids);
        }
    }

    SceneHistory::SceneHistory(Scene& scene, AssetManager& assets, SceneEnvironment& environment, std::function<void()> onEnvironmentChanged)
        : m_scene(scene), m_assets(assets), m_environment(environment), m_onEnvironmentChanged(std::move(onEnvironmentChanged))
    {
    }

    EntityState SceneHistory::Capture(Entity entity, bool withChildren)
    {
        const entt::entity parent = entity.GetComponent<RelationshipComponent>().Parent;

        return EntityState{
            .Node = CaptureNode(entity, m_scene, m_assets, withChildren),
            .Parent = parent != entt::null ? m_scene.GetEntity(parent).GetComponent<NameComponent>().Id : AssetID{},
            .Index = m_scene.GetSiblingIndex(entity)
        };
    }

    SceneHistory::Pending* SceneHistory::FindPending(AssetID id)
    {
        const auto found = std::ranges::find(m_pending, id, &Pending::Id);
        return found != m_pending.end() ? &*found : nullptr;
    }

    void SceneHistory::Record(Entity entity)
    {
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        const AssetID id = entity.GetComponent<NameComponent>().Id;
        if (!FindPending(id))
            m_pending.push_back({ id, Capture(entity, false) });
    }

    void SceneHistory::RecordCreated(Entity entity)
    {
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        const AssetID id = entity.GetComponent<NameComponent>().Id;
        if (!FindPending(id))
            m_pending.push_back({ id, std::nullopt });
    }

    void SceneHistory::RecordDestroy(Entity entity)
    {
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        // What is under it comes back with it, so it isn't recorded on its own as well
        const EntityState state = Capture(entity, true);

        std::vector<AssetID> ids;
        CollectIds(state.Node, ids);
        std::erase_if(m_pending, [&](const Pending& pending) { return pending.Id != state.Node.Id && std::ranges::find(ids, pending.Id) != ids.end(); });

        if (Pending* pending = FindPending(state.Node.Id))
        {
            // Recorded before in this step: as it was then, with what is under it now
            pending->Destroyed = true;
            if (pending->Before)
                pending->Before->Node.Children = state.Node.Children;
            return;
        }

        m_pending.push_back({ state.Node.Id, state, true });
    }

    void SceneHistory::RecordEnvironment()
    {
        if (!m_environmentBefore)
            m_environmentBefore = m_environment;
    }

    bool SceneHistory::Commit(AssetID selected)
    {
        SceneStep step;
        step.Selected = selected;

        for (const Pending& pending : m_pending)
        {
            const Entity entity = pending.Destroyed ? Entity{} : FindEntity(m_scene, pending.Id);
            const std::optional<EntityState> after = entity ? std::optional(Capture(entity, false)) : std::nullopt;

            if (after != pending.Before)
                step.Entities.push_back({ pending.Id, pending.Before, after });
        }

        if (m_environmentBefore && *m_environmentBefore != m_environment)
            step.Environment = std::pair(*m_environmentBefore, m_environment);

        m_pending.clear();
        m_environmentBefore.reset();

        if (step.Entities.empty() && !step.Environment)
            return false;

        // What could be redone was undone before this, and goes
        m_steps.erase(m_steps.begin() + static_cast<std::ptrdiff_t>(m_done), m_steps.end());
        m_steps.push_back(std::move(step));

        if (m_steps.size() > kMaxSteps)
            m_steps.pop_front();

        m_done = m_steps.size();
        return true;
    }

    void SceneHistory::Apply(AssetID id, const std::optional<EntityState>& state)
    {
        Entity entity = FindEntity(m_scene, id);

        if (!state)
        {
            m_scene.DestroyEntity(entity);
            return;
        }

        const Entity parent = state->Parent ? FindEntity(m_scene, state->Parent) : Entity{};

        if (!entity)
        {
            // With the ids it had, and what was under it
            SceneData data;
            data.Roots.push_back(state->Node);
            InstantiateScene(data, m_scene, parent, m_assets, {});
            entity = FindEntity(m_scene, id);
        }
        else
        {
            // The node's transform is relative to the parent it had then
            m_scene.SetParent(entity, parent);
            ApplyNode(entity, state->Node, m_assets);
        }

        if (entity)
            m_scene.SetSiblingIndex(entity, state->Index);
    }

    bool SceneHistory::Undo()
    {
        if (!CanUndo())
            return false;

        const SceneStep& step = m_steps[--m_done];
        m_restoredSelection = step.Selected;

        for (const EntityChange& change : std::views::reverse(step.Entities))
            Apply(change.Id, change.Before);

        if (step.Environment)
        {
            m_environment = step.Environment->first;
            m_onEnvironmentChanged();
        }

        return true;
    }

    bool SceneHistory::Redo()
    {
        if (!CanRedo())
            return false;

        const SceneStep& step = m_steps[m_done++];
        m_restoredSelection = step.Selected;

        for (const EntityChange& change : step.Entities)
            Apply(change.Id, change.After);

        if (step.Environment)
        {
            m_environment = step.Environment->second;
            m_onEnvironmentChanged();
        }

        return true;
    }

    void SceneHistory::Clear()
    {
        m_pending.clear();
        m_environmentBefore.reset();
        m_steps.clear();
        m_restoredSelection = {};
        m_done = 0;
    }
}
