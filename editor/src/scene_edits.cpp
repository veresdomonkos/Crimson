#include "crimson_editor/scene_edits.hpp"

#include <algorithm>
#include <memory>
#include <ranges>
#include <utility>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_serializer.hpp>

namespace crimson::editor
{
    static void CollectIds(const SceneNode& node, std::vector<AssetID>& ids)
    {
        ids.push_back(node.Id);
        for (const SceneNode& child : node.Children)
            CollectIds(child, ids);
    }

    SceneEdits::SceneEdits(Scene& scene, AssetManager& assets, SceneSettings& settings, std::function<void()> onSettingsChanged)
        : m_scene(scene), m_assets(assets), m_settings(settings), m_onSettingsChanged(std::move(onSettingsChanged))
    {
    }

    EntityState SceneEdits::Capture(Entity entity, bool withChildren)
    {
        const entt::entity parent = entity.GetComponent<RelationshipComponent>().Parent;

        return EntityState{
            .Node = CaptureNode(entity, m_scene, m_assets, withChildren),
            .Parent = parent != entt::null ? m_scene.GetEntity(parent).GetComponent<NameComponent>().Id : AssetID{},
            .Index = m_scene.GetSiblingIndex(entity)
        };
    }

    SceneEdits::Pending* SceneEdits::FindPending(AssetID id)
    {
        const auto found = std::ranges::find(m_pending, id, &Pending::Id);
        return found != m_pending.end() ? &*found : nullptr;
    }

    void SceneEdits::Record(Entity entity)
    {
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        const AssetID id = entity.GetComponent<NameComponent>().Id;
        if (!FindPending(id))
            m_pending.push_back({ id, Capture(entity, false) });
    }

    void SceneEdits::RecordCreated(Entity entity)
    {
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        const AssetID id = entity.GetComponent<NameComponent>().Id;
        if (!FindPending(id))
            m_pending.push_back({ id, std::nullopt });
    }

    void SceneEdits::RecordDestroy(Entity entity)
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

    void SceneEdits::RecordSettings()
    {
        if (!m_settingsBefore)
            m_settingsBefore = m_settings;
    }

    // What one commit took from the scene, which puts the entities and the settings back or forward again
    class SceneEdits::Change final : public EditChange
    {
    public:
        Change(SceneEdits& edits, std::vector<EntityChange> entities, std::optional<std::pair<SceneSettings, SceneSettings>> settings, AssetID selected)
            : m_edits(edits), m_entities(std::move(entities)), m_settings(std::move(settings)), m_selected(selected)
        {
        }

        void Undo() override
        {
            m_edits.m_restoredSelection = m_selected;

            for (const EntityChange& change : std::views::reverse(m_entities))
                m_edits.Apply(change.Id, change.Before);

            if (m_settings)
            {
                m_edits.m_settings = m_settings->first;
                m_edits.m_onSettingsChanged();
            }
        }

        void Redo() override
        {
            m_edits.m_restoredSelection = m_selected;

            for (const EntityChange& change : m_entities)
                m_edits.Apply(change.Id, change.After);

            if (m_settings)
            {
                m_edits.m_settings = m_settings->second;
                m_edits.m_onSettingsChanged();
            }
        }
    private:
        SceneEdits& m_edits;
        std::vector<EntityChange> m_entities; // in the order they were recorded
        std::optional<std::pair<SceneSettings, SceneSettings>> m_settings;
        AssetID m_selected; // the entity that was selected before the edit, for undo to select again
    };

    void SceneEdits::Collect(std::vector<std::unique_ptr<EditChange>>& changes)
    {
        std::vector<EntityChange> entities;

        for (const Pending& pending : m_pending)
        {
            const Entity entity = pending.Destroyed ? Entity{} : FindEntity(m_scene, pending.Id);
            const std::optional<EntityState> after = entity ? std::optional(Capture(entity, false)) : std::nullopt;

            if (after != pending.Before)
                entities.push_back({ pending.Id, pending.Before, after });
        }

        std::optional<std::pair<SceneSettings, SceneSettings>> settings;
        if (m_settingsBefore && *m_settingsBefore != m_settings)
            settings = std::pair(*m_settingsBefore, m_settings);

        m_pending.clear();
        m_settingsBefore.reset();

        if (!entities.empty() || settings)
            changes.push_back(std::make_unique<Change>(*this, std::move(entities), std::move(settings), m_selected));
    }

    void SceneEdits::Apply(AssetID id, const std::optional<EntityState>& state)
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

    void SceneEdits::Discard()
    {
        m_pending.clear();
        m_settingsBefore.reset();
        m_selected = {};
        m_restoredSelection = {};
    }
}
