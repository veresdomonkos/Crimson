#pragma once
#include <string>

#include <imgui.h>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor::ui
{
    inline constexpr const char* kAssetPayload = "CRIMSON_ASSET";

    // Makes the last item a drag source for the asset
    inline void DragAssetFromLastItem(const AssetInfo& info)
    {
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kAssetPayload, &info.Id, sizeof(AssetID));
            ImGui::TextUnformatted(info.Path.c_str());
            ImGui::EndDragDropSource();
        }
    }

    // Makes the last item accept dropped assets of a type. Returns the asset on the frame it is dropped. A caller that
    // draws the highlight itself passes ImGuiDragDropFlags_AcceptNoDrawDefaultRect.
    inline AssetID AcceptAssetOnLastItem(const AssetManager& assets, AssetType type, ImGuiDragDropFlags flags = 0)
    {
        AssetID dropped;

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType(kAssetPayload))
            {
                const AssetID id = *static_cast<const AssetID*>(payload->Data);
                const AssetInfo* info = assets.GetInfo(id);

                if (info && info->Type == type && ImGui::AcceptDragDropPayload(kAssetPayload, flags))
                    dropped = id;
            }

            ImGui::EndDragDropTarget();
        }

        return dropped;
    }

    // What a field shows for the asset it refers to: its path, "None", or the path it had for one that is missing
    inline std::string AssetLabel(const AssetManager& assets, AssetID id)
    {
        if (const AssetInfo* info = assets.GetInfo(id))
            return info->Path;

        if (const MissingAssetRef* missing = assets.GetMissing(id))
            return "Missing: " + (missing->Path.empty() ? id.ToString() : missing->Path);

        return id ? "Missing" : "None";
    }

    // The same, for when the type is known when compiling
    template <AssetType T>
    TypedAssetID<T> AcceptAssetOnLastItem(const AssetManager& assets)
    {
        return assets.As<T>(AcceptAssetOnLastItem(assets, T));
    }
}
