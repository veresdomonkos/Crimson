#pragma once
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <crimson/asset_manager/scene_data.hpp>
#include <crimson/scene/entity.hpp>

namespace crimson
{
    class AssetManager;
}

namespace crimson::editor
{
    // One entity as an edit found it or left it
    struct EntityState
    {
        SceneNode Node;  // what is under it only when it was deleted, as only then it comes back with it
        AssetID Parent;  // invalid for a root
        size_t Index = 0; // among its siblings

        bool operator==(const EntityState&) const = default;
    };

    // No Before for an entity the edit created, no After for one it deleted
    struct EntityChange
    {
        AssetID Id;
        std::optional<EntityState> Before;
        std::optional<EntityState> After;
    };

    struct SceneStep
    {
        std::vector<EntityChange> Entities; // in the order they were recorded
        std::optional<std::pair<SceneEnvironment, SceneEnvironment>> Environment;
        AssetID Selected; // the entity that was selected before the edit, for undo to select again
    };

    // Undo and redo for a scene, by the entities each edit changed, as Unity's Undo.RecordObject does it. Code that changes
    // the scene records what it is about to change, and Commit makes a step of what did change once the edit is done.
    // A step keeps the entities' states rather than what was done to them, so an edit that wasn't recorded only means
    // that one can't be undone.
    class SceneHistory
    {
    public:
        static constexpr size_t kMaxSteps = 100;

        // The environment is the scene's, which a step puts back and then calls back about
        SceneHistory(Scene& scene, AssetManager& assets, SceneEnvironment& environment, std::function<void()> onEnvironmentChanged);

        // Before changing an entity. Only its first state in a step counts, so recording it again is cheap.
        // Entities inside instances are left out, as they can't be changed.
        void Record(Entity entity);

        // After creating an entity
        void RecordCreated(Entity entity);

        // Before destroying an entity, which comes back with what is under it
        void RecordDestroy(Entity entity);

        // Before changing the environment
        void RecordEnvironment();

        // Ends the step that was being recorded, which is kept if anything in it changed. Returns whether it was.
        // The selected entity is the one that was selected before the edit.
        bool Commit(AssetID selected = {});

        // Put the scene back to before the last step, or after the next one. They don't commit what is being recorded.
        bool Undo();
        bool Redo();

        // The entity that was selected before the step that Undo or Redo last did
        [[nodiscard]] AssetID RestoredSelection() const { return m_restoredSelection; }

        [[nodiscard]] bool CanUndo() const { return m_done > 0; }
        [[nodiscard]] bool CanRedo() const { return m_done < m_steps.size(); }

        // Forgets every step, like when another scene is opened
        void Clear();
    private:
        struct Pending
        {
            AssetID Id;
            std::optional<EntityState> Before;
            bool Destroyed = false;
        };

        [[nodiscard]] EntityState Capture(Entity entity, bool withChildren);
        [[nodiscard]] Pending* FindPending(AssetID id);
        void Apply(AssetID id, const std::optional<EntityState>& state);

        Scene& m_scene;
        AssetManager& m_assets;
        SceneEnvironment& m_environment;
        std::function<void()> m_onEnvironmentChanged;

        std::vector<Pending> m_pending;
        std::optional<SceneEnvironment> m_environmentBefore;

        std::deque<SceneStep> m_steps;
        AssetID m_restoredSelection;
        size_t m_done = 0; // the steps before this are done, the rest can be redone
    };
}
