#pragma once
#include <functional>
#include <memory>
#include <span>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/project/project.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/app_mode.hpp"
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/scene_camera_controller.hpp"
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

        void PrepareFrame() override;
        [[nodiscard]] const FrameLightingData& Lighting() const override { return m_lighting; }
        void RecordPasses(FrameContext& frame) override;
        void OnImGui() override;
        void Update(float deltaTime) override;
        void RequestClose(std::function<void()> proceed) override;

        [[nodiscard]] EditorContext& GetContext() { return m_context; }
        [[nodiscard]] bool IsAskingAboutUnsavedChanges() const { return static_cast<bool>(m_closeAction); }

        [[nodiscard]] bool IsSceneChanged();

        // The material edits and the scene
        bool SaveAll();

        // What the unsaved changes dialog does
        void SaveAllAndClose();
        void DiscardAllAndClose();
        void CancelClose();
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
        void ResolveAllMeshRenderers();
        void ForgetAssets(std::span<const AssetID> removed);

        // --- The open scene ---
        // The preferred one if it exists, then the first there is, and if there are none it makes one
        [[nodiscard]] SceneID ChooseScene(AssetID preferred);
        void OpenScene(SceneID id);
        void LoadScene(SceneID id);

        // --- Utils ---
        [[nodiscard]] GpuResourceManager& GpuResources() { return *m_services.Graphics.GPUResources; }
    private:
        // Declared so that whatever uses something is destroyed before it
        EditorServices& m_services;
        Project m_project;

        std::unique_ptr<AssetManager> m_assetManager;
        std::unique_ptr<AssetEdits> m_assetEdits;
        std::unique_ptr<AssetThumbnails> m_assetThumbnails;

        PerspectiveCamera m_camera;
        Scene m_scene;
        EditorContext m_context;
        std::unique_ptr<SceneRenderer> m_sceneRenderer;

        // Render targets
        RenderTargetHandle m_mainTarget;

        // Textures
        TextureHandle m_mainColor;

        // What the open scene's file says, to tell if it has changed since
        SceneData m_savedScene;
        SceneEnvironment m_environment;
        float m_sinceSceneCheck = 0.0f;
        float m_sinceShaderCheck = 0.0f;

        // Light
        FrameLightingData m_lighting;
        FrameStats m_frameStats;

        std::unique_ptr<ui::EditorUI> m_ui;
        const ui::ViewportPanel* m_sceneViewport = nullptr;
        std::unique_ptr<SceneCameraController> m_cameraController;

        // What to do once the unsaved changes dialog is answered, empty while it isn't showing
        std::function<void()> m_closeAction;
        bool m_openUnsavedChangesDialog = false;
    };
}
