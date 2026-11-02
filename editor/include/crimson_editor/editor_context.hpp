#pragma once
#include <cstdint>
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
