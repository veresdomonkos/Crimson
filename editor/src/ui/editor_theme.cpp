#include "crimson_editor/ui/editor_theme.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <crimson/core/log.hpp>

#include "crimson_editor/editor_resources.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        ImFont* s_regular = nullptr;
        ImFont* s_bold = nullptr;

        // The bytes are static, so the atlas must not try to free them
        ImFont* AddEmbeddedFont(const char* path, const ImFontConfig& base)
        {
            const EmbeddedFile* file = FindEmbeddedFile(GetEditorResources(), path);
            if (!file)
            {
                LOG_ERROR("[Editor] The embedded font {} is missing", path);
                return nullptr;
            }

            ImFontConfig config = base;
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(file->Data.data()), static_cast<int>(file->Data.size()), theme::FontBody, &config);
        }

        // Unicode's private use area. Inter has glyphs of its own there, which would cover the icons at the same codepoints.
        constexpr ImWchar kPrivateUseArea[] = { 0xE000, 0xF8FF, 0 };

        // Text, with the icons merged in, so text and icons mix in one string
        ImFont* AddTextFont(const char* path)
        {
            ImFontConfig text;
            text.GlyphExcludeRanges = kPrivateUseArea;

            ImFont* font = AddEmbeddedFont(path, text);
            if (!font)
                return nullptr;

            // Icons take the same width as each other, so labels after them line up. They are nearly as tall as the
            // whole font and stand on the baseline, so they go down a little to be centered on the text.
            ImFontConfig icons;
            icons.MergeMode = true;
            icons.GlyphMinAdvanceX = theme::IconSize;
            icons.GlyphOffset = ImVec2(0.0f, theme::IconSize * 0.11f);
            AddEmbeddedFont("fonts/lucide.ttf", icons);

            return font;
        }

        void LoadFonts()
        {
            s_regular = AddTextFont("fonts/Inter_18pt-Regular.ttf");
            s_bold = AddTextFont("fonts/Inter_18pt-Bold.ttf");

            // Without its files the editor still runs, on ImGui's own font
            if (!s_regular)
                s_regular = ImGui::GetIO().Fonts->AddFontDefault();
            if (!s_bold)
                s_bold = s_regular;

            ImGui::GetIO().FontDefault = s_regular;
        }

        void ApplyColors(ImGuiStyle& style)
        {
            using namespace theme;

            ImVec4* colors = style.Colors;

            colors[ImGuiCol_Text]                  = Text;
            colors[ImGuiCol_TextDisabled]          = TextMuted;
            colors[ImGuiCol_TextLink]              = Text;
            colors[ImGuiCol_TextSelectedBg]        = TextSelection;
            colors[ImGuiCol_InputTextCursor]       = Text;

            colors[ImGuiCol_WindowBg]              = PanelBackground;
            colors[ImGuiCol_ChildBg]               = PanelBackground;
            colors[ImGuiCol_PopupBg]               = PopupBackground;
            colors[ImGuiCol_Border]                = Border;
            colors[ImGuiCol_BorderShadow]          = WithAlpha(Border, 0.0f);

            colors[ImGuiCol_FrameBg]               = FrameBackground;
            colors[ImGuiCol_FrameBgHovered]        = Hover;
            colors[ImGuiCol_FrameBgActive]         = Pressed;

            colors[ImGuiCol_TitleBg]               = AppBackground;
            colors[ImGuiCol_TitleBgActive]         = AppBackground;
            colors[ImGuiCol_TitleBgCollapsed]      = AppBackground;
            colors[ImGuiCol_MenuBarBg]             = AppBackground;

            colors[ImGuiCol_ScrollbarBg]           = PanelBackground;
            colors[ImGuiCol_ScrollbarGrab]         = FrameBackground;
            colors[ImGuiCol_ScrollbarGrabHovered]  = Hover;
            colors[ImGuiCol_ScrollbarGrabActive]   = Pressed;

            colors[ImGuiCol_CheckMark]             = Text;
            colors[ImGuiCol_CheckboxSelectedBg]    = FrameBackground;
            colors[ImGuiCol_SliderGrab]            = TextMuted;
            colors[ImGuiCol_SliderGrabActive]      = Text;

            colors[ImGuiCol_Button]                = FrameBackground;
            colors[ImGuiCol_ButtonHovered]         = Hover;
            colors[ImGuiCol_ButtonActive]          = Pressed;

            // Selected rows, tiles and menu items
            colors[ImGuiCol_Header]                = Selection;
            colors[ImGuiCol_HeaderHovered]         = Hover;
            colors[ImGuiCol_HeaderActive]          = Pressed;

            colors[ImGuiCol_Separator]             = Border;
            colors[ImGuiCol_SeparatorHovered]      = Hover;
            colors[ImGuiCol_SeparatorActive]       = Pressed;

            colors[ImGuiCol_ResizeGrip]            = WithAlpha(Hover, 0.0f);
            colors[ImGuiCol_ResizeGripHovered]     = Hover;
            colors[ImGuiCol_ResizeGripActive]      = Pressed;

            // The selected tab is the panel's own color, so it joins the panel, under a thin accent line
            colors[ImGuiCol_Tab]                       = AppBackground;
            colors[ImGuiCol_TabHovered]                = Hover;
            colors[ImGuiCol_TabSelected]               = PanelBackground;
            colors[ImGuiCol_TabSelectedOverline]       = Accent;
            colors[ImGuiCol_TabDimmed]                 = AppBackground;
            colors[ImGuiCol_TabDimmedSelected]         = PanelBackground;
            colors[ImGuiCol_TabDimmedSelectedOverline] = Border;
            colors[ImGuiCol_UnsavedMarker]             = Text;

            colors[ImGuiCol_DockingPreview]        = WithAlpha(Accent, 0.50f);
            colors[ImGuiCol_DockingEmptyBg]        = AppBackground;

            colors[ImGuiCol_PlotLines]             = TextMuted;
            colors[ImGuiCol_PlotLinesHovered]      = Text;
            colors[ImGuiCol_PlotHistogram]         = TextMuted;
            colors[ImGuiCol_PlotHistogramHovered]  = Warning;

            colors[ImGuiCol_TableHeaderBg]         = FrameBackground;
            colors[ImGuiCol_TableBorderStrong]     = Border;
            colors[ImGuiCol_TableBorderLight]      = Border;
            colors[ImGuiCol_TableRowBg]            = WithAlpha(Text, 0.0f);
            colors[ImGuiCol_TableRowBgAlt]         = WithAlpha(Text, 0.03f);
            colors[ImGuiCol_TreeLines]             = Border;

            // A drop target is where the pointer is about to act, so it is drawn like focus
            colors[ImGuiCol_DragDropTarget]        = Accent;
            colors[ImGuiCol_DragDropTargetBg]      = WithAlpha(Accent, 0.15f);

            colors[ImGuiCol_NavCursor]             = Accent;
            colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(Text, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.30f);
            colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
        }
    }

    void ApplyEditorTheme(float scale)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        LoadFonts();

        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding         = theme::Radius;
        style.ChildRounding          = theme::Radius;
        style.FrameRounding          = theme::Radius;
        style.PopupRounding          = theme::Radius;
        style.GrabRounding           = theme::Radius;
        style.ScrollbarRounding      = theme::Radius;
        style.TabRounding            = theme::Radius;
        style.DragDropTargetRounding = 0.0f; // a highlight takes the shape of what it is on, so rounded targets draw their own

        style.WindowPadding     = ImVec2(theme::SpaceS, theme::SpaceS);
        style.FramePadding      = ImVec2(theme::SpaceS, theme::SpaceXS);
        style.ItemSpacing       = ImVec2(theme::SpaceS, theme::SpaceXS);
        style.ItemInnerSpacing  = ImVec2(theme::SpaceXS, theme::SpaceXS);
        style.IndentSpacing     = theme::SpaceL;
        style.ScrollbarSize     = theme::SpaceM;
        style.GrabMinSize       = theme::SpaceS;

        style.WindowBorderSize     = theme::BorderSize;
        style.ChildBorderSize      = theme::BorderSize;
        style.PopupBorderSize      = theme::BorderSize;
        style.FrameBorderSize      = 0.0f;
        style.TabBorderSize        = 0.0f;
        style.TabBarBorderSize     = theme::BorderSize;
        style.TabBarOverlineSize   = theme::AccentLineSize;
        style.DockingSeparatorSize = theme::BorderSize;

        style.FontSizeBase = theme::FontBody;

        ApplyColors(style);

        // The fonts are drawn at the bigger size rather than stretched, so they stay sharp
        scale = std::clamp(scale, 0.5f, 4.0f);
        if (scale != 1.0f)
        {
            style.ScaleAllSizes(scale);
            style.FontScaleDpi = scale;
        }
    }

    namespace theme
    {
        ImVec4 AssetColor(AssetType type)
        {
            switch (type)
            {
                case AssetType::Material: return ImVec4(0.66f, 0.42f, 0.52f, 1.00f);
                case AssetType::Mesh:     return ImVec4(0.38f, 0.54f, 0.68f, 1.00f);
                case AssetType::Shader:   return ImVec4(0.52f, 0.46f, 0.70f, 1.00f);
                case AssetType::Texture:  return ImVec4(0.40f, 0.62f, 0.48f, 1.00f);
                case AssetType::Scene:    return ImVec4(0.70f, 0.50f, 0.36f, 1.00f);
                default:                  return ImVec4(0.42f, 0.43f, 0.46f, 1.00f);
            }
        }

        ImFont* RegularFont() { return s_regular; }
        ImFont* BoldFont() { return s_bold; }

        float Scaled(float size) { return size * ImGui::GetStyle().FontScaleDpi; }

        bool PrimaryButton(const char* label, const ImVec2& size)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentPressed);
            ImGui::PushStyleColor(ImGuiCol_Text, TextOnAccent);
            const bool clicked = ImGui::Button(label, size);
            ImGui::PopStyleColor(4);

            return clicked;
        }

        bool ToggleButton(const char* label, bool on, const ImVec2& size)
        {
            if (on)
                ImGui::PushStyleColor(ImGuiCol_Button, Selection);
            const bool clicked = ImGui::Button(label, size);
            if (on)
                ImGui::PopStyleColor();

            return clicked;
        }

        bool IconTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* icon, const ImVec4& iconColor, const char* label)
        {
            const bool open = ImGui::TreeNodeEx(id, flags, "%s", "");

            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();

            // Where the label would start: after the arrow, and after the frame's padding in a framed node
            float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
            if (flags & ImGuiTreeNodeFlags_Framed)
                x += style.FramePadding.x;
            const float y = std::round((min.y + max.y - ImGui::GetTextLineHeight()) * 0.5f);

            // The node clipped its label to the row, so the drawn one is too
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(min, max, true);
            drawList->AddText(ImVec2(x, y), ImGui::GetColorU32(iconColor), icon);

            if (label && *label)
                drawList->AddText(ImVec2(x + ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x * 2.0f, y), ImGui::GetColorU32(ImGuiCol_Text), label);

            drawList->PopClipRect();
            return open;
        }

        void EmptyHint(const char* text)
        {
            const ScopedFont small(nullptr, FontSmall);

            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float width = std::min(ImGui::CalcTextSize(text).x, available.x);
            const ImVec2 cursor = ImGui::GetCursorPos();

            ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0f, (available.x - width) * 0.5f), cursor.y + ImGui::GetStyle().ItemSpacing.y * 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
}
