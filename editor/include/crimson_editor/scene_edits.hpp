#pragma once
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <crimson/asset_manager/scene_data.hpp>
#include <crimson/scene/entity.hpp>

#include "crimson_editor/edit_history.hpp"

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

    // The scene's part of the edit history, by the entities each edit changed, as Unity's Undo.RecordObject does it. Code
    // that changes the scene records what it is about to change, and the history's Commit takes what did change once the
    // edit is done. A step keeps the entities' states rather than what was done to them, so an edit that wasn't recorded
    // only means that one can't be undone.
    class SceneEdits final : public EditSource
    {
    public:
        // The settings are the scene's, which a step puts back
        SceneEdits(Scene& scene, AssetManager& assets, SceneSettings& settings);

        // Before changing an entity. Only its first state in a step counts, so recording it again is cheap.
        // Entities inside instances are left out, as they can't be changed.
        void Record(Entity entity);

        // After creating an entity
        void RecordCreated(Entity entity);

        // Before destroying an entity, which comes back with what is under it
        void RecordDestroy(Entity entity);

        // Before changing the settings
        void RecordSettings();

        // The entity that is selected before the edit, for undo to select again
        void RecordSelection(AssetID selected) { m_selected = selected; }

        // The entity that was selected before the step that was undone or redone last, once
        [[nodiscard]] AssetID TakeRestoredSelection() { return std::exchange(m_restoredSelection, {}); }

        void Collect(std::vector<std::unique_ptr<EditChange>>& changes) override;
        void Discard() override;
    private:
        class Change;

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
        SceneSettings& m_settings;

        std::vector<Pending> m_pending;
        std::optional<SceneSettings> m_settingsBefore;
        AssetID m_selected;
        AssetID m_restoredSelection;
    };
}
