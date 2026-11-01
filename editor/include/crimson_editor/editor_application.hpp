#pragma once

#include <crimson/core/window.hpp>
#include <crimson/renderer/renderer.hpp>
#include <crimson/ui/imgui_backend.hpp>
#include <crimson/graphics/graphincs_backend.hpp>
#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/frame_stats.hpp"
#include "crimson_editor/ui/editor_ui.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"

namespace crimson::editor
{
	class EditorApplication
	{
	public:
		explicit EditorApplication(RendererAPIType rendererType = RendererAPIType::OpenGL);
        ~EditorApplication() = default;
		void Run();
	private:
	    // --- Setup called from constructor ---
	    void CreateRenderTargets();
	    void CreateScene();
	    void SetupUI();
	    void CreateTextures();

	    // --- Frame ---
	    void RenderFrame();
	    void RecordScenePasses(FrameContext& frame);
	    void RecordUIPass(FrameContext& frame);
	    void UpdateFrameStats();

	    // --- Input / events ---
	    void HandleMove(float deltaTime);
	    void OnEvent(Event& event);

	    // --- Utils ---
	    Entity CreateMeshEntity(const char* name, const char* mesh, const char* material);
	    [[nodiscard]] GpuResourceManager& GpuResources() { return *m_graphicsBackend->GPUResources; }
	private:
	    bool   m_running;
	    double m_lastTime;
	    RendererAPIType m_rendererType;
	    FrameStats m_frameStats;

	    std::unique_ptr<Window>          m_window;
	    std::unique_ptr<GraphicsBackend> m_graphicsBackend;
	    std::unique_ptr<ui::EditorUI>    m_ui;
	    std::unique_ptr<AssetManager>    m_assetManager;

	    PerspectiveCamera m_camera;

	    Scene m_scene;
	    std::unique_ptr<EditorContext> m_context;
	    std::unique_ptr<SceneRenderer> m_sceneRenderer;
	    ui::ViewportPanel* m_sceneViewport = nullptr;

	    // Render targets
	    RenderTargetHandle m_mainTarget;

	    // Textures
	    TextureHandle m_mainColor;
	    TextureHandle m_testTexture;

	    // Light
	    FrameLightingData m_lighting;
	};
}
