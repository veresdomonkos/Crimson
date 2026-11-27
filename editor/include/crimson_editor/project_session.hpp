#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/project/project.hpp>
#include <crimson/renderer/resource_manager.hpp>
#include <crimson/scene/entity.hpp>

#include "crimson_editor/asset_edits.hpp"
#include "crimson_editor/edit_history.hpp"
#include "crimson_editor/recent_projects.hpp"
#include "crimson_editor/recovery_files.hpp"
#include "crimson_editor/scene_edits.hpp"

namespace crimson::editor
{
    // The open project's document: its assets and their edits, the scene and its settings and edits, undo, what is
    // selected, and what to do about the scene file and its recovery copy. It has no window, renderer or UI, which
    // draw it and ask it what to do.
    class ProjectSession
    {
    public:
        // How often the changed mark is looked at, as finding out goes over the whole scene, and how often unsaved changes are copied
        static constexpr float kSceneCheckSeconds = 0.25f;
        static constexpr float kRecoverySeconds = 30.0f;

        // Opens the scene it had open last, or the first there is, or makes one. isEditing says whether a field, the
        // gizmo, the asset picker or anything else is in the middle of a change, which is one step once it is done. It
        // is asked when that has to be known, in Update and when undoing and redoing.
        ProjectSession(Project project, GpuResourceManager& gpu, RecentProjects& recents, std::function<bool()> isEditing);

        ProjectSession(const ProjectSession&) = delete;
        ProjectSession& operator=(const ProjectSession&) = delete;

        [[nodiscard]] const Project& GetProject() const { return m_project; }
        [[nodiscard]] AssetManager& GetAssets() { return m_assets; }
        [[nodiscard]] AssetEdits& GetAssetEdits() { return m_assetEdits; }
        [[nodiscard]] Scene& GetScene() { return m_scene; }
        [[nodiscard]] SceneSettings& GetSettings() { return m_settings; }

        // For undo of the scene: record an entity before changing it, or after creating it. The selected one is recorded already.
        [[nodiscard]] SceneEdits& GetSceneEdits() { return m_sceneEdits; }
        [[nodiscard]] EditHistory& GetHistory() { return m_history; }

        // The scene a built game starts with, which is the project's and not the one the editor opens. Empty if there is
        // none or it isn't there. It follows the scene when that moves, and is cleared with a warning when it is deleted.
        [[nodiscard]] SceneID GetMainScene() const;
        void SetMainScene(SceneID id);

        // The file of the scene. Invalid if the scene couldn't be loaded, and then there is nothing to save to.
        [[nodiscard]] SceneID GetSceneAsset() const { return m_sceneAsset; }

        // As of the last check, which Update makes a few times a second
        [[nodiscard]] bool IsSceneChanged() const { return m_sceneChanged; }
        // Compares the scene with its file now, and keeps the result
        bool CheckSceneChanged();

        // One thing is selected at a time: an entity or an asset, and selecting one clears the other
        [[nodiscard]] Entity GetSelected() const { return m_selected; }
        [[nodiscard]] AssetID GetSelectedAsset() const { return m_selectedAsset; }
        void SelectEntity(Entity entity);
        void SelectAsset(AssetID asset);

        // After an edit done at once rather than with a field, like from a menu or a key, or deferred work that ran. It is
        // a step on the next Update, once nothing is being edited.
        void MarkEdited() { m_edited = true; }

        // Carries out what was asked for and applies the assets' edits, so call it between frames. Ends steps, and
        // checks the scene and copies it for recovery on timers.
        void Update(float deltaTime);

        // The material edits and the scene
        bool SaveAll();

        // Go back to before the last edit, or forward again, after ending the one being recorded. Not while something
        // is being edited, like a field being dragged.
        void Undo();
        void Redo();

        // Replaces the scene with that one on the next Update, after asking about unsaved changes
        void OpenScene(SceneID id);

        // Call after assets are gone, so nothing keeps using them
        void ForgetAssets(std::span<const AssetID> removed);

        // Runs proceed at once if nothing is unsaved. Otherwise the close is pending until one of the three below answers it.
        void RequestClose(std::function<void()> proceed);
        [[nodiscard]] bool IsClosePending() const { return static_cast<bool>(m_closeAction); }

        // Returns whether it saved, as it stays pending if it didn't
        bool SaveAllAndClose();
        void DiscardAllAndClose();
        void CancelClose();

        // The copy of the open scene's unsaved changes, kept in case the editor doesn't close normally. When the one
        // that is being asked about was written, empty if there is none.
        [[nodiscard]] const std::optional<std::filesystem::file_time_type>& GetOfferedRecovery() const { return m_offeredRecovery; }

        // What the timer does
        void WriteRecovery();

        // Restoring happens on the next Update, as it creates GPU resources
        void RestoreRecovery();
        void DiscardRecovery();

        // How many assets the scene that was shown last is missing, once
        [[nodiscard]] size_t TakeMissingAssets();
    private:
        // The preferred one if it exists, then the first there is, and if there are none it makes one
        [[nodiscard]] SceneID ChooseScene(AssetID preferred);
        void LoadScene(SceneID id);
        void ShowScene(const SceneData& data, SceneID id);
        void DoRestoreRecovery();

        // Keeps the project's main scene pointing at its asset after the assets changed
        void SyncMainScene();

        // Logs if the project file can't be written
        void WriteMainScene(AssetRef scene);

        // Asks what to do with the scene's copy if it is newer than the scene's file, and deletes it if it isn't
        void OfferRecovery(SceneID id);
        // Deletes the open scene's copy if this session wrote or restored it
        void ForgetRecovery();

        // Records what the Inspector, the gizmo and Scene Settings may change, while nothing is
        void RecordWatched();

        // After a step was undone or redone
        void FinishUndoRedo(bool applied);
    private:
        // Declared so that whatever uses something is destroyed before it
        Project m_project;
        RecentProjects& m_recents;
        std::function<bool()> m_isEditing;
        RecoveryFiles m_recovery;

        AssetManager m_assets;
        AssetEdits m_assetEdits;
        Scene m_scene;
        SceneSettings m_settings;
        SceneEdits m_sceneEdits;

        // After everything it takes changes from, as it and its changes refer to them
        EditHistory m_history;

        Entity m_selected;
        AssetID m_selectedAsset;

        SceneID m_sceneAsset;
        bool m_sceneChanged = false;

        // What the open scene's file says, to tell if it has changed since
        SceneData m_savedScene;

        // What the open scene's copy holds, if this session wrote or restored it
        std::optional<SceneData> m_writtenRecovery;
        std::optional<std::filesystem::file_time_type> m_offeredRecovery;

        // What to do once the unsaved changes are answered, empty while nothing is asked
        std::function<void()> m_closeAction;

        std::optional<SceneID> m_sceneToLoad;
        bool m_restoreRecovery = false;

        bool m_wasEditing = false;
        bool m_editEnded = false; // editing stopped on the frame before
        bool m_edited = false;

        size_t m_missingAssets = 0;
        uint64_t m_seenAssetsVersion = 0;

        float m_sinceSceneCheck = 0.0f;
        float m_sinceRecovery = 0.0f;
    };
}
