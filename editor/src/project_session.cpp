#include "crimson_editor/project_session.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <crimson/core/log.hpp>
#include <crimson/scene/scene_serializer.hpp>

namespace crimson::editor
{
    // The only part of the recovery files that knows they are scenes
    static std::string RecoveryName(SceneID id)
    {
        return id.ToString() + ".scene";
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

    ProjectSession::ProjectSession(Project project, GpuResourceManager& gpu, RecentProjects& recents, std::function<bool()> isEditing)
        : m_project(std::move(project)),
          m_recents(recents),
          m_isEditing(std::move(isEditing)),
          m_recovery(m_project.DataDir() / "recovery"),
          m_assets(gpu, m_project.AssetsDir(), m_project.DataDir() / "cache"),
          m_assetEdits(m_assets),
          m_sceneEdits(m_scene, m_assets, m_settings)
    {
        // In the order the sources are collected from, which is the order a step's changes are redone in
        m_history.AddSource(m_sceneEdits);
        m_history.AddSource(m_assetEdits);

        LoadScene(ChooseScene(m_recents.GetLastScene(m_project.Root())));

        if (const MainSceneRef& main = m_project.GetMainScene(); main.Id && !m_assets.GetInfo(main.Id) && !m_assets.FindId(main.Path))
            LOG_WARN("[Project] The main scene {} isn't there, so it is kept in case it comes back", main.Path);

        SyncMainScene();
    }

    SceneID ProjectSession::GetMainScene() const
    {
        return m_assets.As<SceneAsset>(m_project.GetMainScene().Id);
    }

    void ProjectSession::SetMainScene(SceneID id)
    {
        const AssetInfo* info = m_assets.GetInfo(id);
        if (!info)
            return;

        if (!m_project.SetMainScene(id, info->Path))
            LOG_ERROR("[Project] Can't write the main scene to {}", Project::kFileName);
    }

    void ProjectSession::SyncMainScene()
    {
        m_seenAssetsVersion = m_assets.Version();

        const MainSceneRef main = m_project.GetMainScene();
        if (!main.Id && main.Path.empty())
            return;

        // A stale id, such as from a scene that was recreated, is found by its path
        AssetID id = m_assets.GetInfo(main.Id) ? main.Id : m_assets.FindId(main.Path);
        const AssetInfo* info = m_assets.GetInfo(id);
        if (!info || !info->Type.Is<SceneAsset>() || (id == main.Id && info->Path == main.Path))
            return;

        if (!m_project.SetMainScene(id, info->Path))
            LOG_ERROR("[Project] Can't write the main scene to {}", Project::kFileName);
    }

    bool ProjectSession::CheckSceneChanged()
    {
        m_sceneChanged = m_sceneAsset && CaptureScene(m_scene, m_settings, m_assets) != m_savedScene;
        return m_sceneChanged;
    }

    void ProjectSession::SelectEntity(Entity entity)
    {
        m_selected = entity;
        m_selectedAsset = {};
    }

    void ProjectSession::SelectAsset(AssetID asset)
    {
        m_selected = {};
        m_selectedAsset = asset;
    }

    void ProjectSession::Update(float deltaTime)
    {
        if (m_assets.Version() != m_seenAssetsVersion)
            SyncMainScene();

        // Between frames, as loading a scene and restoring a copy create GPU resources
        if (m_sceneToLoad)
        {
            LoadScene(*std::exchange(m_sceneToLoad, std::nullopt));
            m_edited = true;
        }

        if (std::exchange(m_restoreRecovery, false))
        {
            DoRestoreRecovery();
            m_edited = true;
        }

        m_assetEdits.Apply();

        // An edit is a step on the frame after it is done, as fields take what the asset picker chose, and the material
        // Inspector prunes to a new shader, once the picker is closed. Those that aren't noticed here are by the check below.
        const bool editing = m_isEditing();
        const bool ended = m_wasEditing && !editing;
        const bool settled = !editing && !ended;
        if (settled && (m_editEnded || m_edited))
            m_history.Commit();

        m_wasEditing = editing;
        m_editEnded = ended;
        m_edited = false;

        m_sinceSceneCheck += deltaTime;
        if (m_sinceSceneCheck >= kSceneCheckSeconds)
        {
            m_sinceSceneCheck = 0.0f;

            if (settled)
                m_history.Commit();

            CheckSceneChanged();
        }

        // Not too often, as each copy is serialized on this thread
        m_sinceRecovery += deltaTime;
        if (m_sinceRecovery >= kRecoverySeconds)
        {
            m_sinceRecovery = 0.0f;
            WriteRecovery();
        }

        // The next edit's state from before it, once nothing is being edited
        if (settled)
            RecordWatched();
    }

    void ProjectSession::ForgetAssets(std::span<const AssetID> removed)
    {
        for (const AssetID id : removed)
        {
            m_assetEdits.Forget(id);

            if (m_selectedAsset == id)
                SelectAsset({});
        }

        if (const AssetID main = m_project.GetMainScene().Id; main && std::ranges::find(removed, main) != removed.end())
        {
            LOG_WARN("[Project] The main scene {} was deleted, so the project has none now", m_project.GetMainScene().Path);

            if (!m_project.SetMainScene({}, {}))
                LOG_ERROR("[Project] Can't write the main scene to {}", Project::kFileName);
        }

        const auto isRemoved = [removed](AssetID id) { return std::ranges::find(removed, id) != removed.end(); };

        if (isRemoved(m_sceneAsset))
        {
            // Nothing is left of the scene for its copy to restore into
            m_recovery.Remove(RecoveryName(m_sceneAsset));
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

    SceneID ProjectSession::ChooseScene(AssetID preferred)
    {
        if (const SceneID scene = m_assets.As<SceneAsset>(preferred))
            return scene;

        // Not the engine's template, which is listed too
        const std::vector<const AssetInfo*> scenes = m_assets.List(AssetType::Of<SceneAsset>());
        const auto first = std::ranges::find_if(scenes, [](const AssetInfo* scene) { return !scene->ReadOnly; });

        return first == scenes.end() ? m_assets.Create<SceneAsset>("scenes/main.scene") : m_assets.As<SceneAsset>((*first)->Id);
    }

    void ProjectSession::OpenScene(SceneID id)
    {
        if (id == m_sceneAsset)
            return;

        RequestClose([this, id] { m_sceneToLoad = id; });
    }

    void ProjectSession::ShowScene(const SceneData& data, SceneID id)
    {
        m_selected = {};
        m_scene.Clear();

        // What was done to the last scene can't be undone in this one
        m_history.Clear();

        InstantiateScene(data, m_scene, {}, m_assets, id);

        m_missingAssets = CountMissing(data.Roots, m_assets);
        m_settings = data.Settings;
    }

    void ProjectSession::LoadScene(SceneID id)
    {
        m_sceneAsset = {};
        m_sceneChanged = false;
        m_writtenRecovery.reset();
        m_offeredRecovery.reset();

        const std::optional<SceneData> data = m_assets.Read(id);
        if (!data)
        {
            m_selected = {};
            m_scene.Clear();
            m_history.Clear();
            return;
        }

        ShowScene(*data, id);

        // Not the file's data, as loading can fill things in that shouldn't count as a change
        m_savedScene = CaptureScene(m_scene, m_settings, m_assets);
        m_sceneAsset = id;

        m_recents.SetLastScene(m_project.Root(), id);

        OfferRecovery(id);
    }

    void ProjectSession::OfferRecovery(SceneID id)
    {
        const std::string name = RecoveryName(id);

        const std::optional<std::filesystem::file_time_type> copied = m_recovery.WrittenAt(name);
        if (!copied)
            return;

        std::error_code error;
        const std::filesystem::file_time_type saved = std::filesystem::last_write_time(m_assets.GetFilePath(id), error);

        // A copy older than the file has nothing the file doesn't, as saving removes it. One with the same time is kept, as
        // some file systems only keep whole seconds, which a copy written right after the file shares.
        if (error || *copied < saved)
        {
            m_recovery.Remove(name);
            return;
        }

        m_offeredRecovery = copied;
    }

    void ProjectSession::WriteRecovery()
    {
        if (!m_sceneAsset)
            return;

        // On this thread, as they read the scene and the assets. Only the write to the disk is on another.
        SceneData current = CaptureScene(m_scene, m_settings, m_assets);

        if (current == m_savedScene)
        {
            // Undoing back to the saved state leaves a copy of this session out of date
            ForgetRecovery();
        }
        else if (m_writtenRecovery != current)
        {
            m_recovery.Write(RecoveryName(m_sceneAsset), scene_data::Write(current, m_assets));
            m_writtenRecovery = std::move(current);
        }
    }

    void ProjectSession::ForgetRecovery()
    {
        // Only a copy of this session. One that was offered and not answered yet may be the only trace of lost work.
        if (!m_writtenRecovery)
            return;

        m_recovery.Remove(RecoveryName(m_sceneAsset));
        m_writtenRecovery.reset();
    }

    void ProjectSession::RestoreRecovery()
    {
        m_restoreRecovery = true;
    }

    void ProjectSession::DoRestoreRecovery()
    {
        if (!m_offeredRecovery || !m_sceneAsset)
            return;

        const SceneID id = m_sceneAsset;
        const std::string name = RecoveryName(id);
        const AssetInfo* info = m_assets.GetInfo(id);

        const std::optional<std::string> text = m_recovery.Read(name);
        std::optional<SceneData> data;
        if (text && info)
            data = scene_data::Parse(*text, m_assets, info->Path);

        if (!data)
        {
            LOG_WARN("[Recovery] The copy of the scene can't be read, so it is deleted");
            m_recovery.Remove(name);
            m_offeredRecovery.reset();
            return;
        }

        // The saved scene stays as it was, which makes what was restored an unsaved change
        ShowScene(*data, id);
        m_writtenRecovery = CaptureScene(m_scene, m_settings, m_assets);
        m_offeredRecovery.reset();
        CheckSceneChanged();
    }

    void ProjectSession::DiscardRecovery()
    {
        if (m_sceneAsset)
            m_recovery.Remove(RecoveryName(m_sceneAsset));

        m_offeredRecovery.reset();
    }

    size_t ProjectSession::TakeMissingAssets()
    {
        return std::exchange(m_missingAssets, 0);
    }

    void ProjectSession::RecordWatched()
    {
        // The Inspector and the gizmo change the selection directly, and Scene Settings the scene's settings
        m_sceneEdits.Record(m_selected);
        m_sceneEdits.RecordSettings();

        m_sceneEdits.RecordSelection(m_selected ? m_selected.GetComponent<NameComponent>().Id : AssetID{});
    }

    void ProjectSession::Undo()
    {
        if (m_isEditing())
            return;

        // An edit that wasn't a step yet is the one to undo
        m_history.Commit();
        FinishUndoRedo(m_history.Undo());
    }

    void ProjectSession::Redo()
    {
        if (m_isEditing())
            return;

        // An edit since the last undo makes a step, which leaves nothing to redo
        m_history.Commit();
        FinishUndoRedo(m_history.Redo());
    }

    void ProjectSession::FinishUndoRedo(bool applied)
    {
        // Taken even when it isn't used, so a step without a scene part can't find an old one
        const AssetID restored = m_sceneEdits.TakeRestoredSelection();

        // An entity that comes back is a new one, so what was selected is found again by its id
        if (applied && !m_selected)
        {
            if (const Entity entity = FindEntity(m_scene, restored))
                SelectEntity(entity);
        }

        RecordWatched();
        CheckSceneChanged();
    }

    bool ProjectSession::SaveAll()
    {
        bool saved = m_assetEdits.SaveAll();

        if (!m_sceneAsset)
        {
            LOG_WARN("[Editor] No scene is open, so no scene was saved");
            return saved;
        }

        SceneData current = CaptureScene(m_scene, m_settings, m_assets);

        if (current != m_savedScene)
        {
            if (m_assets.Save(m_sceneAsset, current))
            {
                m_savedScene = std::move(current);
                m_sceneChanged = false;
                ForgetRecovery();
            }
            else
            {
                saved = false;
            }
        }

        return saved;
    }

    void ProjectSession::RequestClose(std::function<void()> proceed)
    {
        // Closing normally leaves nothing to recover
        proceed = [this, proceed = std::move(proceed)]
        {
            ForgetRecovery();
            proceed();
        };

        if (!m_assetEdits.HasUnsavedChanges() && !CheckSceneChanged())
        {
            proceed();
            return;
        }

        m_closeAction = std::move(proceed);
    }

    bool ProjectSession::SaveAllAndClose()
    {
        // A file that can't be written is in the console, and it is better to stay than to lose the edit
        const bool saved = SaveAll();

        if (saved && m_closeAction)
            std::exchange(m_closeAction, {})();

        return saved;
    }

    void ProjectSession::DiscardAllAndClose()
    {
        m_assetEdits.DiscardAll();

        if (m_closeAction)
            std::exchange(m_closeAction, {})();
    }

    void ProjectSession::CancelClose()
    {
        m_closeAction = {};
    }
}
