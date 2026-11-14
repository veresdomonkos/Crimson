#include "crimson_editor/ui/asset_field.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/asset_tiles.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    std::string AssetFieldText(const AssetManager& assets, AssetID id, AssetType type)
    {
        if (const AssetInfo* info = assets.GetInfo(id))
            return AssetFileName(info->Path);

        if (const MissingAssetRef* missing = assets.GetMissing(id))
            return "Missing: " + (missing->Path.empty() ? id.ToString() : missing->Path);

        return id ? "Missing" : "None (" + std::string(ToString(type)) + ")";
    }

    bool AssetField(const char* id, EditorContext& context, AssetType type, AssetID& current)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const AssetManager& assets = context.Assets;

        const ImGuiID owner = ImGui::GetID(id);
        const float height = ImGui::GetFrameHeight();

        // One frame holds the icon, the name and the picker button, which is drawn over its right end. The frame is the
        // drop target, so its highlight covers all of it.
        ImGui::SetNextItemAllowOverlap();
        const bool frameClicked = ImGui::InvisibleButton(id, ImVec2(std::max(ImGui::CalcItemWidth(), height * 2.0f), height));
        const bool frameHovered = ImGui::IsItemHovered();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        const AssetID dropped = AcceptAssetOnLastItem(assets, type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);

        const float nameRight = max.x - height;
        const bool overName = ImGui::GetIO().MousePos.x < nameRight;
        const bool hovered = frameHovered && overName;

        const AssetInfo* info = assets.GetInfo(current);
        const bool missing = assets.IsMissing(current);

        if (frameClicked && overName && current)
            context.PingedAsset = current;

        if (hovered)
        {
            if (missing)
                ImGui::SetTooltip("The file isn't there. The reference is kept, and works again once it is back.");
            else if (info)
                ImGui::SetTooltip("%s", info->Path.c_str());
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);

        if (context.Picker.IsOpenFor(owner))
            drawList->AddRect(min, max, ImGui::GetColorU32(theme::Accent), style.FrameRounding, 0, theme::Scaled(theme::BorderSize));

        // The picker button, a square at the right end whose icon is centered by what it draws
        ImGui::SetCursorScreenPos(ImVec2(nameRight, min.y));
        ImGui::PushID(id);
        const bool pickerClicked = ImGui::InvisibleButton("##Pick", ImVec2(height, height));
        ImGui::PopID();

        if (pickerClicked)
            context.Picker.Open(owner, type, current, ImVec2(min.x, max.y));

        if (ImGui::IsItemHovered())
        {
            const ImGuiCol color = ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered;
            drawList->AddRectFilled(ImVec2(nameRight, min.y), max, ImGui::GetColorU32(color), style.FrameRounding, ImDrawFlags_RoundCornersRight);
        }

        // Icons are as big as the text, in a box of that size at the middle of their square
        const float iconSize = ImGui::GetFontSize();
        const auto drawIcon = [&](float left, const char* icon, const ImVec4& color)
        {
            const ImVec2 center(left + height * 0.5f, min.y + height * 0.5f);
            DrawTileIcon(drawList, ImVec2(center.x - iconSize * 0.5f, center.y - iconSize * 0.5f), ImVec2(center.x + iconSize * 0.5f, center.y + iconSize * 0.5f), icon, color);
        };

        drawIcon(nameRight, kIconPicker, style.Colors[ImGuiCol_Text]);

        // Not for a missing one, as the empty thumbnail would be kept after the file is back
        const ImTextureID picture = type == AssetType::Texture && info ? context.Thumbnails.Get(assets.As<AssetType::Texture>(current)) : ImTextureID{ 0 };

        if (picture)
        {
            const float inset = std::round(style.FramePadding.y * 0.5f);
            const float side = height - inset * 2.0f;
            drawList->AddImage(picture, ImVec2(min.x + inset, min.y + inset), ImVec2(min.x + inset + side, min.y + inset + side));
        }
        else
        {
            drawIcon(min.x, AssetIcon(type), theme::AssetColor(type));
        }

        const ImVec4 textColor = missing ? theme::Warning : info && info->ReadOnly ? theme::TextMuted : style.Colors[ImGuiCol_Text];
        const std::string text = AssetFieldText(assets, current, type);

        drawList->PushClipRect(ImVec2(min.x, min.y), ImVec2(nameRight, max.y), true);
        drawList->AddText(ImVec2(min.x + height, min.y + style.FramePadding.y), ImGui::GetColorU32(textColor), text.c_str());
        drawList->PopClipRect();

        // ImGui's own highlight is square, so the one for an asset of the type is drawn here with the frame's corners
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType(kAssetPayload) && ImGui::IsMouseHoveringRect(min, max))
        {
            const AssetInfo* dragged = assets.GetInfo(*static_cast<const AssetID*>(payload->Data));
            if (dragged && dragged->Type == type)
            {
                drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_DragDropTargetBg), style.FrameRounding);
                drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_DragDropTarget), style.FrameRounding, 0, style.DragDropTargetBorderSize);
            }
        }

        bool changed = false;

        if (dropped && dropped != current)
        {
            current = dropped;
            changed = true;
        }

        if (const std::optional<AssetID> picked = context.Picker.TakeChoice(owner); picked && *picked != current)
        {
            current = *picked;
            changed = true;
        }

        return changed;
    }
}
