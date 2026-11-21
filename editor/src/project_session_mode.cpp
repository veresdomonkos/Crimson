#include "crimson_editor/project_session_mode.hpp"

#include <crimson/core/log.hpp>
#include <crimson/core/profiler.hpp>
#include <crimson/project/shader_includes.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>

#include "crimson_editor/editor_settings.hpp"
#include "crimson_editor/recent_projects.hpp"
#include "crimson_editor/script_editor.hpp"
#include "crimson_editor/ui/asset_browser_panel.hpp"
#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/hierarchy_panel.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/menus.hpp"
#include "crimson_editor/ui/widgets.hpp"
#include "crimson_editor/ui/inspector_panel.hpp"
#include "crimson_editor/ui/preferences_panel.hpp"
#include "crimson_editor/ui/scene_settings_panel.hpp"
#include "crimson_editor/ui/scene_viewport_panel.hpp"
#include "crimson_editor/ui/stats_panel.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"

namespace crimson::editor
{
    constexpr uint32_t kMainTargetWidth = 1920;
    constexpr uint32_t kMainTargetHeight = 1080;

    static std::string FormatTime(std::filesystem::file_time_type time)
    {
        const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(time));
        const std::time_t seconds = std::chrono::system_clock::to_time_t(system);

        std::ostringstream text;
        text << std::put_time(std::localtime(&seconds), "%Y-%m-%d %H:%M");
        return text.str();
    }

    ProjectSessionMode::ProjectSessionMode(EditorServices& services, Project project)
        : m_services(services),
          m_session(std::move(project), GpuResources(), m_services.Recents, [this] { return IsEditing(); }),
          m_assetThumbnails(std::make_unique<AssetThumbnails>(m_session.GetAssets(), GpuResources())),
          m_sceneView(m_session.GetScene(), m_camera, static_cast<float>(kMainTargetWidth) / kMainTargetHeight),
          m_context{ .Session = m_session, .Thumbnails = *m_assetThumbnails,
                     .OnAssetsRemoved = [this](std::span<const AssetID> removed)
                     {
                         for (const AssetID id : removed)
                             m_assetThumbnails->Release(id);

                         m_session.ForgetAssets(removed);
                     },
                     .OpenCodeFile = [this](const std::filesystem::path& file) { script_editor::Open(m_services.Settings.GetScriptEditor(), file, m_session.GetProject().Root()); },
                     .ShowAsset = [this](AssetID asset) { m_assetBrowser->Ping(asset); } },
          m_notifications([this] { m_console->Show(); })
    {
        LOG_INFO("[Editor] Opening project '{}' at {}", m_session.GetProject().Name(), m_session.GetProject().Root().string());

        // For code editors to find the engine's shader headers, which may have changed since the project was last open
        ExportShaderIncludes(m_session.GetProject());

        m_sceneRenderer = std::make_unique<SceneRenderer>(GpuResources(), *m_services.Graphics.Renderer, m_session.GetAssets());
        CreateRenderTargets();
        SetupRenderer();
        ApplyToneMapping();
        SetupUI();
        SetupCommands();

        m_cameraController = std::make_unique<SceneCameraController>(m_camera, m_services.AppInput, m_services.AppWindow, *m_sceneViewport);

        m_camera.SetPosition(glm::vec3(0.0f, 3.5f, 7.0f));
        m_cameraController->SetPivot(glm::vec3(0.0f));
    }

    ProjectSessionMode::~ProjectSessionMode()
    {
        // Frames still in flight are using all of this
        m_services.Graphics.Device->WaitIdle();

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
        AssetManager& assets = m_session.GetAssets();

        m_sceneRenderer->SetToneMapMaterial(assets.Find<MaterialAsset>("engine/materials/internal/M_ToneMap.mat"), kMainTargetWidth, kMainTargetHeight);
        m_sceneRenderer->SetOutlineMaterial(assets.Find<MaterialAsset>("engine/materials/internal/M_Outline.mat"), kMainTargetWidth, kMainTargetHeight);
    }

    void ProjectSessionMode::SetupUI()
    {
        m_sceneViewport = &m_workspace.AddPanel<ui::SceneViewportPanel>(m_mainColor, 16.0f / 9.0f, m_context, m_sceneView);
        m_workspace.AddPanel<ui::ViewportPanel>(ui::PanelInfo{ .Title = "Shadow Map", .Icon = ui::icons::kTexture, .Slot = ui::DockSlot::RightBottom, .FillsWindow = true },
                                                m_sceneRenderer->GetShadowAtlas(), 1.0f);
        m_assetBrowser = &m_workspace.AddPanel<ui::AssetBrowserPanel>(m_context, m_services.Settings, m_services.AppWindow);
        m_console = &m_workspace.AddPanel<ui::ConsolePanel>();
        m_workspace.AddPanel<ui::HierarchyPanel>(m_context);
        m_workspace.AddPanel<ui::InspectorPanel>(m_context);
        m_workspace.AddPanel<ui::StatsPanel>(m_frameStats, m_services.RendererType);
        m_workspace.AddPanel<ui::SceneSettingsPanel>(m_context);
        m_preferences = &m_workspace.AddPanel<ui::PreferencesPanel>(m_services.Settings, m_services.AppWindow);
    }

    void ProjectSessionMode::SetupCommands()
    {
        // Undoing and redoing can create entities, which load GPU resources, so they wait until after the frame
        m_workspace.AddCommand({ .MenuPath = "File/Save All", .Priority = 0, .Shortcuts = { { Key::S, KeyModifiers::Control } },
                                 .Run = [this] { SaveAll(); } });

        m_workspace.AddCommand({ .MenuPath = "File/Close Project", .Priority = 1,
                                 .Run = [this] { RequestClose(m_services.CloseProject); } });

        m_workspace.AddCommand({ .MenuPath = "Edit/Undo", .Priority = 100, .Shortcuts = { { Key::Z, KeyModifiers::Control } }, .Repeats = true,
                                 .Enabled = [this] { return m_session.GetHistory().CanUndo(); },
                                 .Run = [this] { m_context.Deferred.emplace_back([this] { m_session.Undo(); }); } });

        m_workspace.AddCommand({ .MenuPath = "Edit/Redo", .Priority = 101,
                                 .Shortcuts = { { Key::Y, KeyModifiers::Control }, { Key::Z, KeyModifiers::Control | KeyModifiers::Shift } }, .Repeats = true,
                                 .Enabled = [this] { return m_session.GetHistory().CanRedo(); },
                                 .Run = [this] { m_context.Deferred.emplace_back([this] { m_session.Redo(); }); } });

        m_workspace.AddCommand({ .MenuPath = "Edit/Preferences...", .Priority = 150,
                                 .Run = [this] { m_preferences->Show(); } });

        // While flying, F is a key of the camera
        m_workspace.AddCommand({ .Shortcuts = { { Key::F } },
                                 .Enabled = [this]
                                 {
                                     return m_session.GetSelected() && !m_sceneView.IsLookingThrough()
                                         && !m_services.AppInput.IsMouseButtonDown(MouseButton::Right);
                                 },
                                 .Run = [this] { m_cameraController->Frame(m_session.GetSelected(), m_session.GetAssets()); } });
    }

    bool ProjectSessionMode::RunDeferred()
    {
        const bool any = !m_context.Deferred.empty();

        // An action can add more, which run in the same go
        for (size_t i = 0; i < m_context.Deferred.size(); ++i)
        {
            const std::function<void()> action = std::move(m_context.Deferred[i]);
            action();
        }

        m_context.Deferred.clear();

        return any;
    }

    bool ProjectSessionMode::IsEditing() const
    {
        return ui::input::IsEditingItem() || m_context.GizmoInUse || m_context.Picker.IsOpen();
    }

    bool ProjectSessionMode::SaveAll()
    {
        const bool saved = m_session.SaveAll();

        // What went wrong is already in the Console, and on screen, as each failure logs an error
        if (saved)
            m_notifications.Push(ui::Notifications::Kind::Success, "Saved");

        return saved;
    }

    void ProjectSessionMode::RequestClose(std::function<void()> proceed)
    {
        m_session.RequestClose(std::move(proceed));
    }

    void ProjectSessionMode::DrawUnsavedChangesDialog()
    {
        const ui::Modal modal("Unsaved Changes", "UnsavedChanges", [this] { return m_session.IsClosePending(); });
        if (!modal)
            return;

        ui::Text("The scene or some assets have changes that aren't saved.");
        ui::Spacing();

        const ui::Row row;

        if (ui::PrimaryButton("Save All"))
        {
            // Stays if something couldn't be saved, as it is better than losing the edit
            if (m_session.SaveAllAndClose())
                m_notifications.Push(ui::Notifications::Kind::Success, "Saved");
        }

        if (ui::Button("Discard"))
            m_session.DiscardAllAndClose();

        if (ui::Button("Cancel") || ui::input::KeyPressed(Key::Escape))
            m_session.CancelClose();
    }

    void ProjectSessionMode::DrawRecoveryDialog()
    {
        const ui::Modal modal("Recover Scene", "RecoverScene", [this] { return m_session.GetOfferedRecovery().has_value(); });
        if (!modal)
            return;

        const AssetInfo* info = m_session.GetAssets().GetInfo(m_session.GetSceneAsset());

        ui::Text(std::format("The editor didn't close normally after changes to {} that were never saved.", info ? info->Path : "this scene"));
        ui::Text(std::format("A copy of them from {} was kept.", FormatTime(*m_session.GetOfferedRecovery())));
        ui::Spacing();

        const ui::Row row;

        if (ui::PrimaryButton("Restore"))
            m_session.RestoreRecovery();

        if (ui::Button("Discard"))
            m_session.DiscardRecovery();
    }

    void ProjectSessionMode::Record(FrameRecorder& frame)
    {
        Scene& scene = m_session.GetScene();
        scene.UpdateWorldTransforms();

        FrameLighting& lighting = frame.Lighting();
        lighting.AmbientColor = m_session.GetSettings().Ambient;
        m_sceneRenderer->BuildLighting(scene, lighting);

        const CameraView view = m_sceneView.GetView();

        const PassInfo info{
            .Target = m_mainTarget,
            .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth, .Color = glm::vec4(0.02f, 0.02f, 0.035f, 1.0f) },
            .View = { .ViewProj = view.GetViewProj(), .CameraPosition = view.Position }
        };

        m_sceneRenderer->Record(frame, scene, info, m_session.GetSelected());
    }

    void ProjectSessionMode::DrawUI()
    {
        // The Scene view sets it again while the gizmo is dragged, and may not draw the gizmo at all
        m_context.GizmoInUse = false;

        m_workspace.Draw();

        // Above the panels
        m_notifications.Draw();

        m_context.Picker.Draw(m_session.GetAssets(), m_context.Thumbnails);
        DrawUnsavedChangesDialog();
        DrawRecoveryDialog();
    }

    void ProjectSessionMode::Update(float deltaTime)
    {
        if (RunDeferred())
            m_session.MarkEdited();

        m_session.Update(deltaTime);

        m_sceneView.Update();
        UpdateFrameStats();

        // Often enough for a shader saved in another program to show up about right away
        constexpr float kShaderCheckSeconds = 0.5f;

        m_sinceShaderCheck += deltaTime;
        if (m_sinceShaderCheck >= kShaderCheckSeconds)
        {
            m_sinceShaderCheck = 0.0f;

            m_session.GetAssets().ReloadChanged();
        }

        if (const size_t missing = m_session.TakeMissingAssets())
        {
            const std::string text = missing == 1
                ? "1 asset this scene uses is missing, see the Console"
                : std::to_string(missing) + " assets this scene uses are missing, see the Console";

            m_notifications.Push(ui::Notifications::Kind::Warning, text);
        }

        // The editor camera stays where it is while a scene camera is looked through
        if (m_sceneView.IsLookingThrough())
            m_cameraController->CancelGesture();
        else
            m_cameraController->Update(deltaTime);

        // Last, as undoing and redoing can change the settings
        ApplyToneMapping();
    }

    void ProjectSessionMode::ApplyToneMapping()
    {
        const PostProcessSettings& post = m_session.GetSettings().PostProcessing;
        m_sceneRenderer->SetToneMapping(post.Exposure, post.Mapper);
    }

    void ProjectSessionMode::UpdateFrameStats()
    {
        const auto now = std::chrono::steady_clock::now();
        FrameStats& stats = m_frameStats;

        stats.Render = m_services.Graphics.Renderer->GetStats();

        if (stats.LastFrameTime)
        {
            const double frameSeconds = std::chrono::duration<double>(now - *stats.LastFrameTime).count();

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
}
