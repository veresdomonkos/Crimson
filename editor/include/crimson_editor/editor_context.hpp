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

namespace crimson::editor
{
    // What the panels share
    struct EditorContext
    {
        Scene& ActiveScene;
        AssetManager& Assets;
        AssetEdits& Edits;
        AssetThumbnails& Thumbnails;

        // Call after assets are gone, so nothing keeps using them
        std::function<void(std::span<const AssetID>)> OnAssetsRemoved;

        // Replaces the scene with that one, after asking about unsaved changes
        std::function<void(AssetID)> OpenScene;

        // Opens a code file, such as a shader, in the External Script Editor from the preferences
        std::function<void(const std::filesystem::path&)> OpenCodeFile;

        // The file of ActiveScene. Invalid if the scene couldn't be loaded, and then there is nothing to save to.
        AssetID ActiveSceneAsset;
        bool SceneChanged = false; // looked at a few times a second, as finding out goes over the whole scene

        // One thing is selected at a time: an entity or an asset
        Entity Selected;
        AssetID SelectedAsset;

        // Panels draw in the middle of a frame, when GPU resources can't change. These run right after it.
        std::vector<std::function<void()>> Deferred;

        // Goes up when the project's files change, for the panels that show them
        uint64_t AssetsVersion = 0;

        void SelectEntity(Entity entity)
        {
            Selected = entity;
            SelectedAsset = {};
        }

        void SelectAsset(AssetID asset)
        {
            Selected = {};
            SelectedAsset = asset;
        }
    };
}
