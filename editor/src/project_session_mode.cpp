#include "crimson_editor/project_session_mode.hpp"

#include <crimson/core/log.hpp>
#include <crimson/core/profiler.hpp>
#include <crimson/project/shader_includes.hpp>
#include <crimson/scene/scene_serializer.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

#include "crimson_editor/editor_settings.hpp"
#include "crimson_editor/recent_projects.hpp"
#include "crimson_editor/script_editor.hpp"
#include "crimson_editor/ui/asset_browser_panel.hpp"
#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/hierarchy_panel.hpp"
#include "crimson_editor/ui/inspector_panel.hpp"
#include "crimson_editor/ui/preferences_panel.hpp"
#include "crimson_editor/ui/render_settings_panel.hpp"
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
                     .OnAssetsRemoved = [this](std::span<const AssetID> removed) { ForgetAssets(removed); },
                     .ResolveMeshRenderers = [this] { ResolveAllMeshRenderers(); },
                     .OpenScene = [this](SceneID id) { OpenScene(id); },
                     .OpenCodeFile = [this](const std::filesystem::path& file) { OpenInScriptEditor(m_services.Settings.GetScriptEditor(), file, m_project.Root()); } }
    {
        LOG_INFO("[Editor] Opening project '{}' at {}", m_project.Name(), m_project.Root().string());

        // For code editors to find the engine's shader headers, which may have changed since the project was last open
        ExportShaderIncludes(m_project);

        m_sceneRenderer = std::make_unique<SceneRenderer>(GpuResources(), *m_services.Graphics.Renderer);
        m_ui = std::make_unique<ui::EditorUI>(
            [this] { SaveAll(); },
            [this] { RequestClose(m_services.CloseProject); });

        CreateRenderTargets();
        SetupRenderer();
        SetupUI();
        LoadScene(ChooseScene(m_services.Recents.GetLastScene(m_project.Root())));

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

    void ProjectSessionMode::SetupRenderer()
    {
        m_sceneRenderer->SetToneMapMaterial(m_assetManager->Load<AssetType::Material>("engine/materials/internal/M_ToneMap.mat"), kMainTargetWidth, kMainTargetHeight);
        m_sceneRenderer->SetOutlineMaterial(m_assetManager->Load<AssetType::Material>("engine/materials/internal/M_Outline.mat"), kMainTargetWidth, kMainTargetHeight);
    }

    void ProjectSessionMode::SetupUI()
    {
        auto& imgui = *m_services.Graphics.Imgui;

        m_sceneViewport = &m_ui->AddPanel<ui::SceneViewportPanel>("Scene", imgui.GetOrCreateTextureId(m_mainColor), 16.0f / 9.0f, m_context, m_camera);
        m_ui->AddPanel<ui::ViewportPanel>("Shadow Map", imgui.GetOrCreateTextureId(m_sceneRenderer->GetShadowAtlas()), 1.0f);
        m_ui->AddPanel<ui::AssetBrowserPanel>(m_context, m_services.Settings, m_services.AppWindow); // before the console, for its tab to be in front
        m_ui->AddPanel<ui::ConsolePanel>();
        m_ui->AddPanel<ui::HierarchyPanel>(m_context);
        m_ui->AddPanel<ui::InspectorPanel>(m_context);
        m_ui->AddPanel<ui::StatsPanel>(m_frameStats, m_services.RendererType);
        m_ui->AddPanel<ui::RenderSettingsPanel>(m_context, *m_sceneRenderer, m_environment);

        // Last, for its Edit menu item to come after the Hierarchy's
        m_ui->AddPanel<ui::PreferencesPanel>(m_services.Settings, m_services.AppWindow);
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
        for (const AssetID id : removed)
        {
            m_assetEdits->Forget(id);
            m_assetThumbnails->Release(id);

            if (m_context.SelectedAsset == id)
                m_context.SelectAsset({});
        }

        // The scene's references to what went keep its id, to be saved and to work again when it comes back, which a
        // refresh with nothing removed can be. Also for materials that went away because a shader they used is gone.
        ResolveAllMeshRenderers();

        const auto isRemoved = [removed](AssetID id) { return std::ranges::find(removed, id) != removed.end(); };

        if (isRemoved(m_context.ActiveSceneAsset))
        {
            LoadScene(ChooseScene({}));
            return;
        }

        std::vector<Entity> emptied;

        for (auto [handle, instance] : m_scene.GetRegistry().view<SceneInstanceComponent>().each())
        {
            if (isRemoved(instance.Scene))
                emptied.push_back(m_scene.GetEntity(handle));
        }

        for (const Entity instance : emptied)
        {
            // It can be inside of another one that just went
            if (instance)
                EmptySceneInstance(instance, m_scene);
        }
    }

    SceneID ProjectSessionMode::ChooseScene(AssetID preferred)
    {
        if (const SceneID scene = m_assetManager->As<AssetType::Scene>(preferred))
            return scene;

        const std::vector<const AssetInfo*> scenes = m_assetManager->List(AssetType::Scene);
        return scenes.empty() ? m_assetManager->CreateScene("scenes/main.scene") : m_assetManager->As<AssetType::Scene>(scenes.front()->Id);
    }

    void ProjectSessionMode::OpenScene(SceneID id)
    {
        if (id == m_context.ActiveSceneAsset)
            return;

        RequestClose([this, id]
        {
            m_context.Deferred.emplace_back([this, id] { LoadScene(id); });
        });
    }

    // The references in a scene file to assets that aren't there, not counting what's inside its instances
    static size_t CountMissing(const std::vector<SceneNode>& nodes, const AssetManager& assets)
    {
        size_t missing = 0;

        for (const SceneNode& node : nodes)
        {
            missing += assets.IsMissing(node.Instance);

            // Whatever component they are in
            for (const SceneComponent& component : node.Components)
            {
                for (const FieldValue& value : component.Values)
                {
                    if (const AssetID* id = std::get_if<AssetID>(&value))
                        missing += assets.IsMissing(*id);
                    else if (const auto* ids = std::get_if<std::vector<AssetID>>(&value))
                        missing += std::ranges::count_if(*ids, [&](AssetID id) { return assets.IsMissing(id); });
                }
            }

            missing += CountMissing(node.Children, assets);
        }

        return missing;
    }

    void ProjectSessionMode::LoadScene(SceneID id)
    {
        m_context.Selected = {};
        m_context.ActiveSceneAsset = {};
        m_context.SceneChanged = false;
        m_scene.Clear();

        const std::optional<SceneData> data = m_assetManager->ReadSceneFile(id);
        if (!data)
            return;

        InstantiateScene(*data, m_scene, {}, *m_assetManager, id);

        if (const size_t missing = CountMissing(data->Roots, *m_assetManager))
        {
            const std::string text = missing == 1
                ? "1 asset this scene uses is missing, see the Console"
                : std::to_string(missing) + " assets this scene uses are missing, see the Console";

            m_ui->GetNotifications().Push(ui::Notifications::Kind::Warning, text);
        }

        m_environment = data->Environment;
        m_sceneRenderer->SetToneMapping(m_environment.Exposure, m_environment.Mapper);

        // Not the file's data, as loading can fill things in that shouldn't count as a change
        m_savedScene = CaptureScene(m_scene, m_environment, *m_assetManager);
        m_context.ActiveSceneAsset = id;

        m_services.Recents.SetLastScene(m_project.Root(), id);
    }

    bool ProjectSessionMode::IsSceneChanged()
    {
        return m_context.ActiveSceneAsset && CaptureScene(m_scene, m_environment, *m_assetManager) != m_savedScene;
    }

    bool ProjectSessionMode::SaveAll()
    {
        bool saved = m_assetEdits->SaveAll();

        if (!m_context.ActiveSceneAsset)
        {
            LOG_WARN("[Editor] No scene is open, so no scene was saved");
            return saved;
        }

        SceneData current = CaptureScene(m_scene, m_environment, *m_assetManager);

        if (current != m_savedScene)
        {
            if (m_assetManager->SaveScene(m_context.ActiveSceneAsset, current))
            {
                m_savedScene = std::move(current);
                m_context.SceneChanged = false;
            }
            else
            {
                saved = false;
            }
        }

        // What went wrong is already in the Console, and on screen, as each failure logs an error
        if (saved)
            m_ui->GetNotifications().Push(ui::Notifications::Kind::Success, "Saved");

        return saved;
    }

    void ProjectSessionMode::RequestClose(std::function<void()> proceed)
    {
        if (!m_assetEdits->HasUnsavedChanges() && !IsSceneChanged())
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
        if (SaveAll())
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

        ImGui::TextUnformatted("The scene or some assets have changes that aren't saved.");
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

    void ProjectSessionMode::Record(FrameRecorder& frame)
    {
        m_scene.UpdateWorldTransforms();

        FrameLighting& lighting = frame.Lighting();
        lighting.AmbientColor = m_environment.Ambient;
        m_sceneRenderer->BuildLighting(m_scene, lighting);

        const PassInfo info{
            .Target = m_mainTarget,
            .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = glm::vec4(0.02f, 0.02f, 0.035f, 1.0f) },
            .View = { .ViewProj = m_camera.GetViewProj(), .CameraPosition = m_camera.GetPosition() }
        };

        m_sceneRenderer->Record(frame, m_scene, info, m_context.Selected);
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

        constexpr float kSceneCheckSeconds = 0.25f;

        m_sinceSceneCheck += deltaTime;
        if (m_sinceSceneCheck >= kSceneCheckSeconds)
        {
            m_sinceSceneCheck = 0.0f;
            m_context.SceneChanged = IsSceneChanged();
        }

        // Often enough for a shader saved in another program to show up about right away
        constexpr float kShaderCheckSeconds = 0.5f;

        m_sinceShaderCheck += deltaTime;
        if (m_sinceShaderCheck >= kShaderCheckSeconds)
        {
            m_sinceShaderCheck = 0.0f;

            if (!m_assetManager->ReloadChangedShaders().empty())
                ResolveAllMeshRenderers();
        }

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
            SaveAll();

        if (!ImGui::GetIO().WantTextInput && !m_services.AppInput.IsMouseButtonDown(MouseButton::Right)
            && ImGui::IsKeyPressed(ImGuiKey_F, false) && m_context.Selected)
        {
            m_cameraController->Frame(m_context.Selected);
        }
    }
}
