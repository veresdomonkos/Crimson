#include "crimson_editor/project_session_mode.hpp"

#include <crimson/core/log.hpp>
#include <crimson/core/profiler.hpp>
#include <algorithm>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>

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

    ProjectSessionMode::ProjectSessionMode(EditorServices& services, Project project)
        : m_services(services),
          m_project(std::move(project)),
          m_assetManager(std::make_unique<AssetManager>(GpuResources(), m_project.AssetsDir())),
          m_assetEdits(std::make_unique<AssetEdits>(*m_assetManager)),
          m_assetThumbnails(std::make_unique<AssetThumbnails>(*m_assetManager, *m_services.Graphics.Imgui)),
          m_context{ .ActiveScene = m_scene, .Assets = *m_assetManager, .Edits = *m_assetEdits, .Thumbnails = *m_assetThumbnails,
                     .OnAssetsRemoved = [this](std::span<const AssetID> removed) { ForgetAssets(removed); } }
    {
        LOG_INFO("[Editor] Opening project '{}' at {}", m_project.Name(), m_project.Root().string());

        m_sceneRenderer = std::make_unique<SceneRenderer>(GpuResources(), *m_services.Graphics.Renderer);
        m_ui = std::make_unique<ui::EditorUI>(
            [this] { m_assetEdits->SaveAll(); },
            [this] { RequestClose(m_services.CloseProject); });

        CreateRenderTargets();
        CreateScene();
        SetupUI();

        m_cameraController = std::make_unique<SceneCameraController>(m_camera, m_services.AppInput, *m_sceneViewport);

        m_camera.SetPosition(glm::vec3(0.0f, 3.5f, 7.0f));
        m_cameraController->SetPivot(glm::vec3(0.0f));
    }

    ProjectSessionMode::~ProjectSessionMode()
    {
        // Frames still in flight are using all of this
        m_services.Graphics.Device->WaitIdle();

        auto& imgui = *m_services.Graphics.Imgui;

        imgui.ReleaseTexture(m_mainColor);
        imgui.ReleaseTexture(m_sceneRenderer->GetShadowAtlas());

        GpuResources().DestroyRenderTarget(m_mainTarget);
    }

    void ProjectSessionMode::CreateRenderTargets()
    {
        RenderTargetInfo mainInfo{
            .Width = kMainTargetWidth,
            .Height = kMainTargetHeight,
            .ColorFormats = { TextureFormat::RGBA8 }
        };
        m_mainTarget = GpuResources().CreateRenderTarget(mainInfo);
        m_mainColor  = GpuResources().GetColorAttachment(m_mainTarget, 0);
    }

    Entity ProjectSessionMode::CreateMeshEntity(const char* name, const char* mesh, const char* material)
    {
        const AssetID meshAsset = m_assetManager->FindId(mesh);
        if (!meshAsset)
            return {};

        const Entity entity = m_scene.CreateEntity(name);

        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.MeshAsset = meshAsset;
        renderer.MaterialAssets = { m_assetManager->FindId(material) };
        ResolveMeshRenderer(renderer, *m_assetManager);

        return entity;
    }

    void ProjectSessionMode::CreateScene()
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

        // Only the demo project has these meshes, any other project starts with just the lights
        if (const Entity floor = CreateMeshEntity("Floor", "meshes/floor.obj", "materials/floor.mat"))
            floor.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, kFloorHeight, 0.0f);

        if (const Entity pyramid = CreateMeshEntity("Pyramid", "meshes/pyramid.obj", "materials/pyramid.mat"))
            pyramid.GetComponent<TransformComponent>().Position = glm::vec3(-1.5f, kFloorHeight, 0.0f);

        // The bunny's file is in its own units and not centered, so fit it to the floor
        if (const Entity bunny = CreateMeshEntity("Bunny", "meshes/stanford-bunny.obj", "materials/pyramid.mat"))
        {
            const MeshBounds& bounds = bunny.GetComponent<MeshRendererComponent>().Resolved.Bounds;
            const float bunnyScale = kBunnyHeight / std::max(bounds.Max.y - bounds.Min.y, 0.0001f);
            const glm::vec3 bunnyOrigin((bounds.Min.x + bounds.Max.x) * 0.5f, bounds.Min.y, (bounds.Min.z + bounds.Max.z) * 0.5f);

            auto& bunnyTransform = bunny.GetComponent<TransformComponent>();
            bunnyTransform.Scale = glm::vec3(bunnyScale);
            bunnyTransform.Position = glm::vec3(1.2f, kFloorHeight, 0.5f) - bunnyScale * bunnyOrigin;
        }

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

        m_sceneRenderer->SetToneMapMaterial(m_assetManager->LoadMaterial("engine/materials/tonemap.mat"), kMainTargetWidth, kMainTargetHeight);
        m_sceneRenderer->SetShadowMaterial(m_assetManager->LoadMaterial("engine/materials/shadow.mat"));
        m_sceneRenderer->SetOutlineMaterials(m_assetManager->LoadMaterial("engine/materials/outline_mask.mat"), m_assetManager->LoadMaterial("engine/materials/outline.mat"), kMainTargetWidth, kMainTargetHeight);
    }

    void ProjectSessionMode::SetupUI()
    {
        auto& imgui = *m_services.Graphics.Imgui;

        m_sceneViewport = &m_ui->AddPanel<ui::SceneViewportPanel>("Scene", imgui.GetOrCreateTextureId(m_mainColor), 16.0f / 9.0f, m_context, m_camera);
        m_ui->AddPanel<ui::ViewportPanel>("Shadow Map", imgui.GetOrCreateTextureId(m_sceneRenderer->GetShadowAtlas()), 1.0f);
        m_ui->AddPanel<ui::AssetBrowserPanel>(m_context, m_services.AppWindow); // before the console, for its tab to be in front
        m_ui->AddPanel<ui::ConsolePanel>();
        m_ui->AddPanel<ui::HierarchyPanel>(m_context);
        m_ui->AddPanel<ui::InspectorPanel>(m_context);
        m_ui->AddPanel<ui::StatsPanel>(m_frameStats, m_services.RendererType);
    }

    void ProjectSessionMode::RunDeferred()
    {
        // An action can add more, which run in the same go
        for (size_t i = 0; i < m_context.Deferred.size(); ++i)
        {
            const std::function<void()> action = std::move(m_context.Deferred[i]);
            action();
        }

        m_context.Deferred.clear();

        if (m_assetEdits->Apply())
            ResolveAllMeshRenderers();
    }

    void ProjectSessionMode::ResolveAllMeshRenderers()
    {
        for (auto [handle, renderer] : m_scene.GetRegistry().view<MeshRendererComponent>().each())
            ResolveMeshRenderer(renderer, *m_assetManager);
    }

    void ProjectSessionMode::ForgetAssets(std::span<const AssetID> removed)
    {
        if (removed.empty())
            return;

        for (const AssetID id : removed)
        {
            m_assetEdits->Forget(id);
            m_assetThumbnails->Release(id);

            if (m_context.SelectedAsset == id)
                m_context.SelectAsset({});
        }

        const auto isRemoved = [removed](AssetID id) { return std::ranges::find(removed, id) != removed.end(); };

        for (auto [handle, renderer] : m_scene.GetRegistry().view<MeshRendererComponent>().each())
        {
            if (isRemoved(renderer.MeshAsset))
                renderer.MeshAsset = {};

            for (AssetID& material : renderer.MaterialAssets)
            {
                if (isRemoved(material))
                    material = {};
            }
        }

        // Also for materials that went away because a shader they used is gone
        ResolveAllMeshRenderers();
    }

    void ProjectSessionMode::RequestClose(std::function<void()> proceed)
    {
        if (!m_assetEdits->HasUnsavedChanges())
        {
            proceed();
            return;
        }

        m_closeAction = std::move(proceed);
        m_openUnsavedChangesDialog = true;
    }

    void ProjectSessionMode::SaveAllAndClose()
    {
        // A file that can't be written is in the console, and it is better to stay than to lose the edit
        if (m_assetEdits->SaveAll())
            std::exchange(m_closeAction, {})();
    }

    void ProjectSessionMode::DiscardAllAndClose()
    {
        m_assetEdits->DiscardAll();
        std::exchange(m_closeAction, {})();
    }

    void ProjectSessionMode::CancelClose()
    {
        m_closeAction = {};
    }

    void ProjectSessionMode::DrawUnsavedChangesDialog()
    {
        static constexpr const char* kPopup = "Unsaved Changes";

        if (m_openUnsavedChangesDialog)
        {
            ImGui::OpenPopup(kPopup);
            m_openUnsavedChangesDialog = false;
        }

        if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("Some assets have changes that aren't saved.");
        ImGui::Spacing();

        bool close = false;

        if (ImGui::Button("Save All"))
        {
            SaveAllAndClose();
            close = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            DiscardAllAndClose();
            close = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            CancelClose();
            close = true;
        }

        if (close)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void ProjectSessionMode::PrepareFrame()
    {
        m_scene.UpdateWorldTransforms();
        m_sceneRenderer->BuildLighting(m_scene, m_lighting);
    }

    void ProjectSessionMode::RecordPasses(FrameContext& frame)
    {
        RenderPassInfo info{
            .Target = m_mainTarget,
            .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f),
            .ViewProj = m_camera.GetViewProj(),
            .CameraPosition = m_camera.GetPosition()
        };

        m_sceneRenderer->RecordPasses(frame, m_scene, m_lighting, info, m_context.Selected);
    }

    void ProjectSessionMode::OnImGui()
    {
        m_ui->Draw();
        DrawUnsavedChangesDialog();
    }

    void ProjectSessionMode::Update(float deltaTime)
    {
        RunDeferred();
        UpdateFrameStats();

        m_cameraController->Update(deltaTime);
        HandleShortcuts();
    }

    void ProjectSessionMode::UpdateFrameStats()
    {
        const double now = glfwGetTime();
        FrameStats& stats = m_frameStats;

        stats.Render = m_services.Graphics.Renderer->GetStats();

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

    void ProjectSessionMode::HandleShortcuts()
    {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            m_assetEdits->SaveAll();

        if (!ImGui::GetIO().WantTextInput && !m_services.AppInput.IsMouseButtonDown(MouseButton::Right)
            && ImGui::IsKeyPressed(ImGuiKey_F, false) && m_context.Selected)
        {
            m_cameraController->Frame(m_context.Selected);
        }
    }
}
