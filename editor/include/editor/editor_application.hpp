#pragma once

#include <crimson/core/window.hpp>
#include <crimson/renderer/renderer.hpp>
#include <crimson/ui/imgui_backend.hpp>
#include <crimson/graphics/graphincs_backend.hpp>

#include "editor/frame_stats.hpp"
#include "ui/editor_ui.hpp"

namespace crimson::editor
{
    struct Mesh
    {
        VertexBufferHandle VB;
        IndexBufferHandle IB;
    };

	class EditorApplication
	{
	public:
		explicit EditorApplication(RendererAPIType rendererType = RendererAPIType::OpenGL);
        ~EditorApplication() = default;
		void Run();
	private:
	    // --- Setup called from constructor ---
	    void CreateMeshes();
	    void CreateShadersAndMaterials();
	    void CreateRenderTargets();
	    void SetupLighting();
	    void SetupUI();

	    // --- Frame ---
	    void RenderFrame();
	    void RecordShadowPass(FrameContext& frame);
	    void RecordMainPass(FrameContext& frame);
	    void RecordUIPass(FrameContext& frame);
	    void UpdateFrameStats(float frameTimeMs, float renderMs);

	    // --- Input / events ---
	    void HandleMove(float deltaTime);
	    void OnEvent(Event& event);

	    // --- Utils ---
	    Mesh CreateMesh(std::span<const std::byte> vertices, std::span<const uint32_t> indices);
	    [[nodiscard]] GpuResourceManager& GpuResources() { return *m_graphicsBackend->GPUResources; }
	private:
	    bool   m_running;
	    double m_lastTime;

	    std::unique_ptr<Window>          m_window;
	    std::unique_ptr<GraphicsBackend> m_graphicsBackend;
	    std::unique_ptr<ui::EditorUI>    m_ui;

	    PerspectiveCamera m_camera;
	    FrameStats m_frameStats;

	    // Geometry
	    Mesh m_floor;
	    Mesh m_pyramid;

	    // Shaders
	    ShaderHandle m_mainShader;
	    ShaderHandle m_shadowShader;

	    // Materials
	    MaterialHandle m_floorMat;
	    MaterialHandle m_pyramidMat;
	    MaterialHandle m_shadowMat;

	    // Render targets
	    RenderTargetHandle m_shadowTarget;
	    RenderTargetHandle m_mainTarget;

	    // Textures
	    TextureHandle m_shadowDepth;
	    TextureHandle m_mainColor;

	    // Light
	    glm::mat4 m_lightViewProj{1.0f};
	    FrameLightingData m_lighting;
	};
}
