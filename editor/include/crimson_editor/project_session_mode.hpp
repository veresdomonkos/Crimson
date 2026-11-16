#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/project/project.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/app_mode.hpp"
#include "crimson_editor/edit_history.hpp"
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/recovery_files.hpp"
#include "crimson_editor/scene_camera_controller.hpp"
#include "crimson_editor/scene_edits.hpp"
#include "crimson_editor/scene_view.hpp"
#include "crimson_editor/ui/editor_ui.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"

namespace crimson::editor
{
    // The editor with one project open. Everything that belongs to the project lives and dies with this.
    class ProjectSessionMode final : public AppMode
    {
    public:
        ProjectSessionMode(EditorServices& services, Project project);
        ~ProjectSessionMode() override;

        void Record(FrameRecorder& frame) override;
        void OnImGui() override;
        void Update(float deltaTime) override;
        void RequestClose(std::function<void()> proceed) override;

        [[nodiscard]] EditorContext& GetContext() { return m_context; }
        [[nodiscard]] bool IsAskingAboutUnsavedChanges() const { return static_cast<bool>(m_closeAction); }

        [[nodiscard]] SceneView& GetSceneView() { return m_sceneView; }
        [[nodiscard]] EditHistory& GetHistory() { return m_history; }
        [[nodiscard]] SceneSettings& GetSettings() { return m_settings; }

        [[nodiscard]] bool IsSceneChanged();

        // The material edits and the scene
        bool SaveAll();

        // Go back to before the last edit, or forward again, after ending the one being recorded. Not while something
        // is being edited, like a field being dragged.
        void Undo();
        void Redo();

        // What the unsaved changes dialog does
        void SaveAllAndClose();
        void DiscardAllAndClose();
        void CancelClose();

        // The copy of the open scene's unsaved changes, kept in case the editor doesn't close normally
        [[nodiscard]] bool IsAskingAboutRecovery() const { return m_offeredRecovery.has_value(); }

        // What the timer does, and what the recovery dialog does
        void WriteRecovery();
        void RestoreRecovery();
        void DiscardRecovery();
    private:
        // --- Setup called from constructor ---
        void CreateRenderTargets();
        void SetupRenderer();
        void SetupUI();

        void UpdateFrameStats();
        void HandleShortcuts();
        void DrawUnsavedChangesDialog();

        // --- Assets changing ---
        void RunDeferred();
        void ForgetAssets(std::span<const AssetID> removed);

        // --- The open scene ---
        // The preferred one if it exists, then the first there is, and if there are none it makes one
        [[nodiscard]] SceneID ChooseScene(AssetID preferred);
        void OpenScene(SceneID id);
        void LoadScene(SceneID id);
        void ShowScene(const SceneData& data, SceneID id);

        // --- Recovery copies ---
        // Asks what to do with the scene's copy if it is newer than the scene's file, and deletes it if it isn't
        void OfferRecovery(SceneID id);
        // Deletes the open scene's copy if this session wrote or restored it
        void ForgetRecovery();
        void DrawRecoveryDialog();

        // A field, the gizmo, the asset picker or anything else in the middle of being changed, which is one step once it is done
        [[nodiscard]] bool IsEditing() const;

        // Records what the Inspector, the gizmo and Scene Settings may change, while nothing is
        void RecordWatched();

        // After a step was undone or redone
        void FinishUndoRedo(bool applied);

        // --- Utils ---
        [[nodiscard]] GpuResourceManager& GpuResources() { return *m_services.Graphics.GPUResources; }
    private:
        // Declared so that whatever uses something is destroyed before it
        EditorServices& m_services;
        Project m_project;
        RecoveryFiles m_recovery;

        std::unique_ptr<AssetManager> m_assetManager;
        std::unique_ptr<AssetEdits> m_assetEdits;
        std::unique_ptr<AssetThumbnails> m_assetThumbnails;

        PerspectiveCamera m_camera;
        Scene m_scene;

        // What the Scene view is seen from, the editor camera or a scene camera. Part of the editor, never saved.
        SceneView m_sceneView;

        SceneSettings m_settings;
        SceneEdits m_sceneEdits;

        // After everything it takes changes from, as it and its changes refer to them
        EditHistory m_history;
        ui::AssetTypeRegistry m_assetTypes;
        EditorContext m_context;
        std::unique_ptr<SceneRenderer> m_sceneRenderer;

        // Render targets
        RenderTargetHandle m_mainTarget;

        // Textures
        TextureHandle m_mainColor;

        // What the open scene's file says, to tell if it has changed since
        SceneData m_savedScene;

        // What the open scene's copy holds, if this session wrote or restored it
        std::optional<SceneData> m_writtenRecovery;
        // When the copy that is being asked about was written, empty while the recovery dialog isn't showing
        std::optional<std::filesystem::file_time_type> m_offeredRecovery;
        bool m_openRecoveryDialog = false;
        float m_sinceRecovery = 0.0f;

        bool m_wasEditing = false;
        bool m_editEnded = false; // editing stopped on the frame before
        float m_sinceSceneCheck = 0.0f;
        float m_sinceShaderCheck = 0.0f;

        FrameStats m_frameStats;

        std::unique_ptr<ui::EditorUI> m_ui;
        const ui::ViewportPanel* m_sceneViewport = nullptr;
        std::unique_ptr<SceneCameraController> m_cameraController;

        // What to do once the unsaved changes dialog is answered, empty while it isn't showing
        std::function<void()> m_closeAction;
        bool m_openUnsavedChangesDialog = false;
    };
}
