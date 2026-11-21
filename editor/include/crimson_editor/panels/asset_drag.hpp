#pragma once
#include <string>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/ui/drag_drop.hpp"

namespace crimson::editor
{
    // What a field shows for the asset it refers to: its path, "None", or the path it had for one that is missing
    inline std::string AssetLabel(const AssetManager& assets, AssetID id)
    {
        if (const AssetInfo* info = assets.GetInfo(id))
            return info->Path;

        if (const MissingAssetRef* missing = assets.GetMissing(id))
            return "Missing: " + (missing->Path.empty() ? id.ToString() : missing->Path);

        return id ? "Missing" : "None";
    }

    namespace asset_drag
    {
        inline constexpr const char* kPayload = "CRIMSON_ASSET";

        // Makes the last item a drag source for the asset
        inline void DragFromLastItem(const AssetInfo& info)
        {
            ui::drag_drop::Source(kPayload, info.Id, info.Path);
        }

        // Makes the last item accept dropped assets of a type. Returns the asset on the frame it is dropped. A caller that
        // draws the highlight itself passes DropLook::None.
        inline AssetID AcceptOnLastItem(const AssetManager& assets, AssetType type, ui::drag_drop::DropLook look = ui::drag_drop::DropLook::Default)
        {
            const auto dropped = ui::drag_drop::Accept<AssetID>(kPayload, [&](const AssetID& id)
            {
                const AssetInfo* info = assets.GetInfo(id);
                return info && info->Type == type;
            }, look);

            return dropped.value_or(AssetID{});
        }

        // The highlight of a drop target with rounded corners, while an asset of the type is dragged over it. The default one is
        // square, which suits windows and rows, so a rounded target accepts with DropLook::None and draws this.
        inline void DrawDropHighlight(const AssetManager& assets, AssetType type, const ui::Rect& rect, float rounding)
        {
            const auto dragged = ui::drag_drop::Dragged<AssetID>(kPayload);
            if (!dragged)
                return;

            const AssetInfo* info = assets.GetInfo(*dragged);
            if (info && info->Type == type)
                ui::drag_drop::Highlight(rect, rounding);
        }

        // The same, for when the type is known when compiling
        template <typename T>
        TypedAssetID<T> AcceptOnLastItem(const AssetManager& assets)
        {
            return assets.As<T>(AcceptOnLastItem(assets, AssetType::Of<T>()));
        }
    }
}
