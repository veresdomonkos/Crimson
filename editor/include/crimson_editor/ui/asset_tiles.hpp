#pragma once
#include <string>

#include <imgui.h>

namespace crimson::editor::ui
{
    // The last part of an asset's path
    [[nodiscard]] std::string AssetFileName(const std::string& path);

    // An icon as big as fits in a box, centered by what it draws rather than by its place in the font, which leaves
    // room around icons that isn't the same on every side
    void DrawTileIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* icon, const ImVec4& color);

    // Where a tile's parts go. Its frame is drawn here rather than by a Selectable, whose highlight reaches past the
    // tile into the spacing around it, so the icon and the name are centered in exactly what shows as the tile.
    struct TileLayout
    {
        ImVec2 IconMin;
        ImVec2 IconMax;
        ImVec2 LabelPosition;
        float LabelWidth = 0.0f;
        bool Clicked = false;
    };

    TileLayout DrawTileFrame(const char* id, float width, bool selected);

    // A name centered under its tile, shortened with "..." if it is wider than the tile. The tooltip has the whole name.
    void DrawClippedText(ImDrawList* drawList, const ImVec2& position, float width, const char* text, ImU32 color);
}
