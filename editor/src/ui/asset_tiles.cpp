#include "crimson_editor/ui/asset_tiles.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor::ui
{
    std::string AssetFileName(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // The first character of a UTF-8 string, such as an icon
    static unsigned int FirstCodepoint(const char* text)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(text);

        if (bytes[0] < 0x80) return bytes[0];
        if ((bytes[0] & 0xE0) == 0xC0) return ((bytes[0] & 0x1Fu) << 6) | (bytes[1] & 0x3Fu);
        if ((bytes[0] & 0xF0) == 0xE0) return ((bytes[0] & 0x0Fu) << 12) | ((bytes[1] & 0x3Fu) << 6) | (bytes[2] & 0x3Fu);
        return ((bytes[0] & 0x07u) << 18) | ((bytes[1] & 0x3Fu) << 12) | ((bytes[2] & 0x3Fu) << 6) | (bytes[3] & 0x3Fu);
    }

    void DrawTileIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* icon, const ImVec4& color)
    {
        const float size = std::min(max.x - min.x, max.y - min.y);
        ImFont* font = ImGui::GetFont();
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        ImVec2 position;
        if (const ImFontGlyph* glyph = font->GetFontBaked(size)->FindGlyphNoFallback(static_cast<ImWchar>(FirstCodepoint(icon))))
        {
            position = ImVec2(center.x - (glyph->X0 + glyph->X1) * 0.5f, center.y - (glyph->Y0 + glyph->Y1) * 0.5f);
        }
        else
        {
            const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
            position = ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
        }

        drawList->AddText(font, size, position, ImGui::GetColorU32(color), icon);
    }

    TileLayout DrawTileFrame(const char* id, float width, bool selected)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad = std::round(theme::Scaled(theme::SpaceS));
        const float iconHeight = std::round(width * 0.62f);
        const float height = pad + iconHeight + style.ItemInnerSpacing.y + ImGui::GetTextLineHeight() + pad;

        TileLayout layout;
        layout.Clicked = ImGui::InvisibleButton(id, ImVec2(width, height));

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        const ImGuiCol background = selected ? ImGuiCol_Header : ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_COUNT;
        if (background != ImGuiCol_COUNT)
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(background), theme::Scaled(theme::Radius));

        layout.IconMin = ImVec2(min.x + pad, min.y + pad);
        layout.IconMax = ImVec2(max.x - pad, min.y + pad + iconHeight);
        layout.LabelPosition = ImVec2(min.x + pad * 0.5f, layout.IconMax.y + style.ItemInnerSpacing.y);
        layout.LabelWidth = width - pad;
        return layout;
    }

    void DrawClippedText(ImDrawList* drawList, const ImVec2& position, float width, const char* text, ImU32 color)
    {
        static constexpr const char* kEllipsis = "...";

        std::string shown = text;
        if (ImGui::CalcTextSize(shown.c_str()).x > width)
        {
            const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;

            // Whole UTF-8 characters come off the end, never half of one: continuation bytes, then the byte they follow
            while (!shown.empty() && ImGui::CalcTextSize(shown.c_str()).x + ellipsisWidth > width)
            {
                while (!shown.empty())
                {
                    const auto byte = static_cast<unsigned char>(shown.back());
                    shown.pop_back();

                    if ((byte & 0xC0) != 0x80)
                        break;
                }
            }

            shown += kEllipsis;
        }

        const float textWidth = ImGui::CalcTextSize(shown.c_str()).x;
        const ImVec2 start(position.x + std::max((width - textWidth) * 0.5f, 0.0f), position.y);
        const ImVec4 clip(position.x, position.y, position.x + width, position.y + ImGui::GetTextLineHeight());

        drawList->AddText(nullptr, 0.0f, start, color, shown.c_str(), nullptr, 0.0f, &clip);
    }
}
