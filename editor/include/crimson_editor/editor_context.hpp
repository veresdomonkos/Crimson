#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/entity.hpp>

#include "crimson_editor/asset_edits.hpp"
#include "crimson_editor/asset_thumbnails.hpp"
#include "crimson_editor/scene_history.hpp"
#include "crimson_editor/ui/asset_picker.hpp"

namespace crimson::editor
{
    // What the panels share
    struct EditorContext
    {
        Scene& ActiveScene;
        AssetManager& Assets;
        AssetEdits& Edits;
        AssetThumbnails& Thumbnails;

        // For undo: record an entity before changing it, or after creating it. The selected one is recorded already.
        SceneHistory& History;

        // Call after assets are gone, so nothing keeps using them
        std::function<void(std::span<const AssetID>)> OnAssetsRemoved;

        // Replaces the scene with that one, after asking about unsaved changes
        std::function<void(SceneID)> OpenScene;

        // Opens a code file, such as a shader, in the External Script Editor from the preferences
        std::function<void(const std::filesystem::path&)> OpenCodeFile;

        // The file of ActiveScene. Invalid if the scene couldn't be loaded, and then there is nothing to save to.
        SceneID ActiveSceneAsset;
        bool SceneChanged = false; // looked at a few times a second, as finding out goes over the whole scene

        // An edit is a step once it is done: a field's when it stops being active, the gizmo's when this goes back to
        // false. The Scene view sets it on each frame the gizmo is dragged.
        bool GizmoInUse = false;

        // Set after an edit done at once rather than with a field, like from a menu or a key, for it to be a step right away
        bool SceneEdited = false;

        // One thing is selected at a time: an entity or an asset
        Entity Selected;
        AssetID SelectedAsset;

        // The one picker that can be open: fields open it and take what it chose the next frame
        ui::AssetPicker Picker;

        // Set by a field to show the asset in the Assets panel, which clears it
        AssetID PingedAsset;

        // Panels draw in the middle of a frame, when GPU resources can't change. These run right after it.
        std::vector<std::function<void()>> Deferred;

        // Goes up when the project's files change, for the panels that show them
        uint64_t AssetsVersion = 0;

        void SelectEntity(Entity entity)
        {
            Selected = entity;
            SelectedAsset = {};

            // Fields of one kind share their ImGui ids from one selection to the next, so a choice must not reach the next
            Picker.Reset();
        }

        void SelectAsset(AssetID asset)
        {
            Selected = {};
            SelectedAsset = asset;
            Picker.Reset();
        }
    };
}
