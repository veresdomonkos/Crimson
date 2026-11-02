#pragma once
#include <imgui.h>

#include <crimson/asset_manager/asset_manager.hpp>

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

    // Makes the last item accept dropped assets of a type. Returns the asset on the frame it is dropped.
    inline AssetID AcceptAssetOnLastItem(const AssetManager& assets, AssetType type)
    {
        AssetID dropped;

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType(kAssetPayload))
            {
                const AssetID id = *static_cast<const AssetID*>(payload->Data);
                const AssetInfo* info = assets.GetInfo(id);

                if (info && info->Type == type && ImGui::AcceptDragDropPayload(kAssetPayload))
                    dropped = id;
            }

            ImGui::EndDragDropTarget();
        }

        return dropped;
    }
}
