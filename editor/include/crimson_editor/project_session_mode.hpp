#pragma once
#include <memory>

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
    private:
        // --- Setup called from constructor ---
        void CreateRenderTargets();
        void CreateScene();
        void SetupUI();
        void CreateTextures();

        void UpdateFrameStats();
        void HandleShortcuts();

        // --- Utils ---
        // An invalid entity if the project doesn't have the mesh
        Entity CreateMeshEntity(const char* name, const char* mesh, const char* material);
        [[nodiscard]] GpuResourceManager& GpuResources() { return *m_services.Graphics.GPUResources; }
    private:
        // Declared so that whatever uses something is destroyed before it
        EditorServices& m_services;
        Project m_project;

        std::unique_ptr<AssetManager> m_assetManager;

        PerspectiveCamera m_camera;
        Scene m_scene;
        EditorContext m_context;
        std::unique_ptr<SceneRenderer> m_sceneRenderer;

        // Render targets
        RenderTargetHandle m_mainTarget;

        // Textures
        TextureHandle m_mainColor;
        TextureHandle m_testTexture;

        // Light
        FrameLightingData m_lighting;
        FrameStats m_frameStats;

        std::unique_ptr<ui::EditorUI> m_ui;
        const ui::ViewportPanel* m_sceneViewport = nullptr;
        std::unique_ptr<SceneCameraController> m_cameraController;
    };
}
