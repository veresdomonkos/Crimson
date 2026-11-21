#include "crimson_editor/ui/painter.hpp"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include <imgui.h>

#include "imgui_convert.hpp"
#include "theme_style.hpp"

namespace crimson::editor::ui
{
    // The first character of a UTF-8 string, such as an icon
    static unsigned int FirstCodepoint(const char* text)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(text);

        if (bytes[0] < 0x80) return bytes[0];
        if ((bytes[0] & 0xE0) == 0xC0) return ((bytes[0] & 0x1Fu) << 6) | (bytes[1] & 0x3Fu);
        if ((bytes[0] & 0xF0) == 0xE0) return ((bytes[0] & 0x0Fu) << 12) | ((bytes[1] & 0x3Fu) << 6) | (bytes[2] & 0x3Fu);
        return ((bytes[0] & 0x07u) << 18) | ((bytes[1] & 0x3Fu) << 12) | ((bytes[2] & 0x3Fu) << 6) | (bytes[3] & 0x3Fu);
    }

    static ImDrawList* List(void* list) { return static_cast<ImDrawList*>(list); }

    Painter::Painter()
        : m_list(ImGui::GetWindowDrawList())
    {
    }

    void Painter::FillRect(const Rect& rect, Color color, float rounding, Corners corners)
    {
        const ImDrawFlags flags = corners == Corners::Right ? ImDrawFlags_RoundCornersRight : ImDrawFlags_None;

        List(m_list)->AddRectFilled(ToImVec2(rect.Min), ToImVec2(rect.Max), ToImU32(color), rounding, flags);
    }

    void Painter::StrokeRect(const Rect& rect, Color color, float rounding, float thickness)
    {
        List(m_list)->AddRect(ToImVec2(rect.Min), ToImVec2(rect.Max), ToImU32(color), rounding, ImDrawFlags_None, thickness);
    }

    void Painter::Text(glm::vec2 position, Color color, std::string_view text, Font font, float size)
    {
        List(m_list)->AddText(theme::FontOf(font), size, ToImVec2(position), ToImU32(color), text.data(), text.data() + text.size());
    }

    void Painter::ClippedText(glm::vec2 position, float width, std::string_view text, Color color)
    {
        static constexpr const char* kEllipsis = "...";

        std::string shown(text);
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

        List(m_list)->AddText(nullptr, 0.0f, start, ToImU32(color), shown.c_str(), nullptr, 0.0f, &clip);
    }

    void Painter::Icon(const Rect& rect, std::string_view icon, Color color)
    {
        const std::string glyphs(icon);
        const glm::vec2 extent = rect.Size();
        const float size = std::min(extent.x, extent.y);
        ImFont* font = ImGui::GetFont();
        const glm::vec2 center = rect.Center();

        glm::vec2 position;
        if (const ImFontGlyph* glyph = font->GetFontBaked(size)->FindGlyphNoFallback(static_cast<ImWchar>(FirstCodepoint(glyphs.c_str()))))
        {
            position = glm::vec2(center.x - (glyph->X0 + glyph->X1) * 0.5f, center.y - (glyph->Y0 + glyph->Y1) * 0.5f);
        }
        else
        {
            const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyphs.c_str());
            position = glm::vec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
        }

        List(m_list)->AddText(font, size, ToImVec2(position), ToImU32(color), glyphs.c_str());
    }

    void Painter::Image(TextureHandle texture, const Rect& rect)
    {
        List(m_list)->AddImage(ToImTextureID(texture), ToImVec2(rect.Min), ToImVec2(rect.Max));
    }

    ClipScope::ClipScope(const Rect& rect)
    {
        ImGui::GetWindowDrawList()->PushClipRect(ToImVec2(rect.Min), ToImVec2(rect.Max), true);
    }

    ClipScope::~ClipScope()
    {
        ImGui::GetWindowDrawList()->PopClipRect();
    }
}
