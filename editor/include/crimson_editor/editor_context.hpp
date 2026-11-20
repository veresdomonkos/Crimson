#pragma once
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/asset_thumbnails.hpp"
#include "crimson_editor/project_session.hpp"
#include "crimson_editor/ui/asset_picker.hpp"

namespace crimson::editor
{
    // What the panels share
    struct EditorContext
    {
        // The open project's document: everything panels read and change goes through it
        ProjectSession& Session;
        AssetThumbnails& Thumbnails;

        // Call after assets are gone, so nothing keeps using them
        std::function<void(std::span<const AssetID>)> OnAssetsRemoved;

        // Opens a code file, such as a shader, in the External Script Editor from the preferences
        std::function<void(const std::filesystem::path&)> OpenCodeFile;

        // An edit is a step once it is done: a field's when it stops being active, the gizmo's when this goes back to
        // false. The Scene view sets it on each frame the gizmo is dragged.
        bool GizmoInUse = false;

        // The one picker that can be open: fields open it and take what it chose the next frame
        ui::AssetPicker Picker;

        // Set by a field to show the asset in the Assets panel, which clears it
        AssetID PingedAsset;

        // Panels draw in the middle of a frame, when GPU resources can't change. These run right after it.
        std::vector<std::function<void()>> Deferred;
    };
}
