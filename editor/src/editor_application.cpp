#include "crimson_editor/editor_application.hpp"

#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/core/profiler.hpp>
#include <crimson/renderer/renderer_api.hpp>
#include <crimson/scene/scene_renderer.hpp>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>

#include "crimson/asset_manager/importers/material_importer.hpp"
#include "crimson/asset_manager/importers/mesh_importer.hpp"
#include "crimson/asset_manager/importers/shader_importer.hpp"
#include "crimson/asset_manager/importers/texture_importer.hpp"
#include "crimson_editor/ui/asset_browser_panel.hpp"
#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/hierarchy_panel.hpp"
#include "crimson_editor/ui/inspector_panel.hpp"
#include "crimson_editor/ui/scene_viewport_panel.hpp"
#include "crimson_editor/ui/stats_panel.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"


namespace crimson::editor
{
    static constexpr uint32_t kMainTargetWidth = 1920;
    static constexpr uint32_t kMainTargetHeight = 1080;

	EditorApplication::EditorApplication(RendererAPIType rendererType)
        : m_running(true), m_lastTime(0.0), m_rendererType(rendererType)
    {
        m_window          = Window::Create(rendererType, WindowData{ "Crimson Editor", 1280, 720, BIND_FN(OnEvent) });
        m_graphicsBackend = GraphicsBackend::Create(rendererType, *m_window);
        m_ui              = std::make_unique<ui::EditorUI>(*m_graphicsBackend->Imgui);
	    m_assetManager    = std::make_unique<AssetManager>(*m_graphicsBackend->GPUResources, "assets");
	    m_assetManager->RegisterImporter<TextureImporter>();
	    m_assetManager->RegisterImporter<ShaderImporter>();
	    m_assetManager->RegisterImporter<MaterialImporter>();
	    m_assetManager->RegisterImporter<MeshImporter>();
	    m_assetManager->ScanDirectory();

        m_context       = std::make_unique<EditorContext>(m_scene, *m_assetManager, m_assetManager->FindByPath("materials/default.mat"));
        m_sceneRenderer = std::make_unique<SceneRenderer>(GpuResources(), *m_graphicsBackend->Renderer);

        CreateRenderTargets();
        CreateTextures();
        CreateScene();
        SetupUI();

        m_input = std::make_unique<Input>(*m_window);
        m_cameraController = std::make_unique<SceneCameraController>(m_camera, *m_input, *m_sceneViewport);

        m_camera.SetPosition(glm::vec3(0.0f, 3.5f, 7.0f));
        m_cameraController->SetPivot(glm::vec3(0.0f));
    }

    void EditorApplication::CreateRenderTargets()
    {
        RenderTargetInfo mainInfo{
            .Width = kMainTargetWidth,
            .Height = kMainTargetHeight,
            .ColorFormats = { TextureFormat::RGBA8 },
            .DepthFormat = TextureFormat::Depth32F
        };
        m_mainTarget = GpuResources().CreateRenderTarget(mainInfo);
        m_mainColor  = GpuResources().GetColorAttachment(m_mainTarget, 0);
    }

    Entity EditorApplication::CreateMeshEntity(const char* name, const char* mesh, const char* material)
    {
        const Entity entity = m_scene.CreateEntity(name);

        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.MeshAsset = m_assetManager->FindByPath(mesh);
        renderer.MaterialAssets = { m_assetManager->FindByPath(material) };
        ResolveMeshRenderer(renderer, *m_assetManager, m_context->DefaultMaterial);

        return entity;
    }

    void EditorApplication::CreateScene()
    {
        constexpr float kFloorHeight = -0.5f;
        constexpr float kBunnyHeight = 1.5f;

        const auto addLight = [this](const char* name, Entity parent, const LightComponent& light, const glm::vec3& position, const glm::vec3& direction)
        {
            const Entity entity = m_scene.CreateEntity(name, parent);
            entity.AddComponent<LightComponent>(light);

            // Points the light's -Z axis along the direction
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Position = position;
            transform.RotationDegrees = glm::degrees(glm::eulerAngles(glm::quatLookAt(glm::normalize(direction), glm::vec3(0.0f, 1.0f, 0.0f))));
        };

        const Entity floor = CreateMeshEntity("Floor", "meshes/floor.obj", "materials/floor.mat");
        floor.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, kFloorHeight, 0.0f);

        const Entity pyramid = CreateMeshEntity("Pyramid", "meshes/pyramid.obj", "materials/pyramid.mat");
        pyramid.GetComponent<TransformComponent>().Position = glm::vec3(-1.5f, kFloorHeight, 0.0f);

        // The bunny's file is in its own units and not centered, so fit it to the floor
        const Entity bunny = CreateMeshEntity("Bunny", "meshes/stanford-bunny.obj", "materials/pyramid.mat");
        const MeshBounds& bounds = bunny.GetComponent<MeshRendererComponent>().Resolved.Bounds;
        const float bunnyScale = kBunnyHeight / std::max(bounds.Max.y - bounds.Min.y, 0.0001f);
        const glm::vec3 bunnyOrigin((bounds.Min.x + bounds.Max.x) * 0.5f, bounds.Min.y, (bounds.Min.z + bounds.Max.z) * 0.5f);

        auto& bunnyTransform = bunny.GetComponent<TransformComponent>();
        bunnyTransform.Scale = glm::vec3(bunnyScale);
        bunnyTransform.Position = glm::vec3(1.2f, kFloorHeight, 0.5f) - bunnyScale * bunnyOrigin;

        const Entity lights = m_scene.CreateEntity("Lights");

        addLight("Sun", lights, { .Type = LightType::Directional, .Color = glm::vec3(1.0f), .Intensity = 0.5f, .CastsShadow = true },
            glm::vec3(0.0f, 8.0f, 0.0f), glm::vec3(-0.5f, -1.0f, -0.3f));

        addLight("Orange Point", lights, { .Type = LightType::Point, .Color = glm::vec3(1.0f, 0.5f, 0.2f), .Intensity = 1.5f, .Range = 8.0f, .CastsShadow = true },
            glm::vec3(2.0f, 3.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

        addLight("Red Point", lights, { .Type = LightType::Point, .Color = glm::vec3(0.8f, 0.1f, 0.2f), .Intensity = 1.5f, .Range = 8.0f },
            glm::vec3(-2.0f, 3.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

        addLight("Blue Spot", lights, { .Type = LightType::Spot, .Color = glm::vec3(0.2f, 0.5f, 1.0f), .Intensity = 2.5f, .Range = 12.0f, .InnerConeDegrees = 20.0f, .OuterConeDegrees = 35.0f, .CastsShadow = true },
            glm::vec3(0.0f, 4.0f, 3.0f), glm::vec3(0.0f, -1.0f, -0.8f));

        m_lighting.AmbientColor = glm::vec3(0.05f);

        const auto loadMaterial = [this](const char* path)
        {
            const AssetID id = m_assetManager->FindByPath(path);
            return id ? m_assetManager->GetAsset<MaterialHandle>(id).value_or(MaterialHandle::Invalid()) : MaterialHandle::Invalid();
        };

        m_sceneRenderer->SetShadowMaterial(loadMaterial("materials/shadow.mat"));
        m_sceneRenderer->SetOutlineMaterials(loadMaterial("materials/outline_mask.mat"), loadMaterial("materials/outline.mat"), kMainTargetWidth, kMainTargetHeight);
    }

    void EditorApplication::SetupUI()
    {
        auto& imgui = *m_graphicsBackend->Imgui;

        m_sceneViewport = &m_ui->AddPanel<ui::SceneViewportPanel>("Scene", imgui.GetOrCreateTextureId(m_mainColor), 16.0f / 9.0f, *m_context, m_camera);
        m_ui->AddPanel<ui::ViewportPanel>("Shadow Map", imgui.GetOrCreateTextureId(m_sceneRenderer->GetShadowAtlas()), 1.0f);
	    if (m_testTexture)
	        m_ui->AddPanel<ui::ViewportPanel>("Test Texture", imgui.GetOrCreateTextureId(m_testTexture), 875.0f / 350.0f);
	    m_ui->AddPanel<ui::ConsolePanel>();
	    m_ui->AddPanel<ui::HierarchyPanel>(*m_context);
	    m_ui->AddPanel<ui::InspectorPanel>(*m_context);
	    m_ui->AddPanel<ui::AssetBrowserPanel>();
	    m_ui->AddPanel<ui::StatsPanel>(m_frameStats, m_rendererType);
    }

    void EditorApplication::CreateTextures()
    {
	    if (const AssetID id = m_assetManager->FindByPath("textures/test.jpg"))
	        m_testTexture = m_assetManager->GetAsset<TextureHandle>(id).value_or(TextureHandle::Invalid());
    }

    void EditorApplication::Run()
    {
        while (m_running)
        {
            const double currentTime = glfwGetTime();
            const auto deltaTime = static_cast<float>(currentTime - m_lastTime);
            m_lastTime = currentTime;

            {
                CRIMSON_PROFILE_SCOPE("Poll Events");
                m_window->PollEvents();
            }
            RenderFrame();

            m_input->Update();
            m_cameraController->Update(deltaTime);
            HandleShortcuts();

            CRIMSON_PROFILE_FRAME();
        }
    }

    void EditorApplication::RenderFrame()
    {
        CRIMSON_PROFILE_FUNCTION();

        auto& renderer = *m_graphicsBackend->Renderer;

        m_scene.UpdateWorldTransforms();
        m_sceneRenderer->BuildLighting(m_scene, m_lighting);

        auto frame = renderer.BeginFrame(m_lighting);

        if (!frame.ShouldRender())
            return;

	    m_graphicsBackend->Imgui->NewFrame();

        RecordScenePasses(frame);
        RecordUIPass(frame);

        renderer.EndFrame(frame);

        UpdateFrameStats();
    }

    void EditorApplication::UpdateFrameStats()
    {
        const double now = glfwGetTime();
        FrameStats& stats = m_frameStats;

        stats.Render = m_graphicsBackend->Renderer->GetStats();

        if (stats.LastFrameTime > 0.0)
        {
            const double frameSeconds = now - stats.LastFrameTime;

            stats.WindowSeconds += frameSeconds;
            stats.WindowWaitSeconds += stats.Render.WaitSeconds;
            stats.WindowWorstSeconds = std::max(stats.WindowWorstSeconds, frameSeconds);
            ++stats.WindowFrames;

            if (stats.WindowSeconds >= 1.0)
            {
                const double frames = stats.WindowFrames;

                stats.AverageFps = static_cast<float>(frames / stats.WindowSeconds);
                stats.AverageFrameMs = static_cast<float>(stats.WindowSeconds / frames * 1000.0);
                stats.WorstFrameMs = static_cast<float>(stats.WindowWorstSeconds * 1000.0);
                stats.AverageWaitMs = static_cast<float>(stats.WindowWaitSeconds / frames * 1000.0);
                stats.AverageCpuMs = stats.AverageFrameMs - stats.AverageWaitMs;

                stats.WindowSeconds = 0.0;
                stats.WindowWaitSeconds = 0.0;
                stats.WindowWorstSeconds = 0.0;
                stats.WindowFrames = 0;
            }
        }

        stats.LastFrameTime = now;
    }

    void EditorApplication::RecordScenePasses(FrameContext& frame)
    {
        RenderPassInfo info{
            .Target = m_mainTarget,
            .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f),
            .ViewProj = m_camera.GetViewProj(),
            .CameraPosition = m_camera.GetPosition()
        };

        m_sceneRenderer->RecordPasses(frame, m_scene, m_lighting, info, m_context->Selected);
    }

    void EditorApplication::RecordUIPass(FrameContext& frame)
    {
        RawPassInfo info{
            .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(0.09f, 0.10f, 0.11f, 1.0f),
            .Callback = [this](const NativeFrameHandles& handles) { m_ui->Draw(handles); }
        };
        frame.AddRawPass(info);
    }

    void EditorApplication::HandleShortcuts()
    {
        if (!ImGui::GetIO().WantTextInput && !m_input->IsMouseButtonDown(MouseButton::Right)
            && ImGui::IsKeyPressed(ImGuiKey_F, false) && m_context->Selected)
        {
            m_cameraController->Frame(m_context->Selected);
        }
    }

	void EditorApplication::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			m_running = false;
			return true;
		});

	    dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event)
	    {
	        //m_camera.SetAspect(static_cast<float>(event.GetWidth()) / static_cast<float>(event.GetHeight()));
	        return false;
	    });
	}
}
