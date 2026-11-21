#pragma once
#include <crimson/asset_manager/asset_types.hpp>
#include <crimson/renderer/resource_handles.hpp>

#include "crimson_editor/ui/painter.hpp"
#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    // What tells the kinds of assets apart: the color they are drawn in, and their icon
    [[nodiscard]] ui::Color AssetColor(AssetType type);
    [[nodiscard]] const char* AssetIcon(AssetType type);

    // An asset as its picture, a square centered in the icon's box, or its icon in its color where it has none
    void DrawAssetPicture(ui::Painter& painter, const ui::Rect& icon, AssetType type, TextureHandle picture);
}
