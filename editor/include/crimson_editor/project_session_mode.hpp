#pragma once
#include <functional>
#include <memory>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/project/project.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/app_mode.hpp"
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/project_session.hpp"
#include "crimson_editor/scene_camera_controller.hpp"
#include "crimson_editor/scene_view.hpp"
#include "crimson_editor/ui/asset_browser_panel.hpp"
#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/notifications.hpp"
#include "crimson_editor/ui/preferences_panel.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"
#include "crimson_editor/ui/workspace.hpp"

namespace crimson::editor
{
    // The editor with one project open: draws the project's ProjectSession, and asks it what to do. Everything that
    // belongs to the project lives and dies with this.
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
        [[nodiscard]] SceneView& GetSceneView() { return m_sceneView; }
        [[nodiscard]] ProjectSession& GetSession() { return m_session; }
        [[nodiscard]] ui::Workspace& GetWorkspace() { return m_workspace; }
    private:
        // --- Setup called from constructor ---
        void CreateRenderTargets();
        void SetupRenderer();
        void SetupUI();
        void SetupCommands();

        // Gives the renderer the scene's tone mapping, which it ignores when nothing changed
        void ApplyToneMapping();

        void UpdateFrameStats();
        void DrawUnsavedChangesDialog();
        void DrawRecoveryDialog();

        // Whether any ran
        bool RunDeferred();

        // Saves everything, and says so if it worked
        bool SaveAll();

        // A field, the gizmo, the asset picker or anything else in the middle of being changed, which is one step once it is done
        [[nodiscard]] bool IsEditing() const;

        // --- Utils ---
        [[nodiscard]] GpuResourceManager& GpuResources() { return *m_services.Graphics.GPUResources; }
    private:
        // Declared so that whatever uses something is destroyed before it
        EditorServices& m_services;
        ProjectSession m_session;
        std::unique_ptr<AssetThumbnails> m_assetThumbnails;

        PerspectiveCamera m_camera;

        // What the Scene view is seen from, the editor camera or a scene camera. Part of the editor, never saved.
        SceneView m_sceneView;

        EditorContext m_context;
        std::unique_ptr<SceneRenderer> m_sceneRenderer;

        // Render targets
        RenderTargetHandle m_mainTarget;

        // Textures
        TextureHandle m_mainColor;

        FrameStats m_frameStats;

        ui::Workspace m_workspace;
        const ui::ViewportPanel* m_sceneViewport = nullptr;
        ui::AssetBrowserPanel* m_assetBrowser = nullptr;
        ui::ConsolePanel* m_console = nullptr;
        ui::PreferencesPanel* m_preferences = nullptr;

        // Clicking a message shows the Console
        ui::Notifications m_notifications;
        std::unique_ptr<SceneCameraController> m_cameraController;

        float m_sinceShaderCheck = 0.0f;
    };
}
