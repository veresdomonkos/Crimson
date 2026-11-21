#include "theme_style.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui.h>

#include <crimson/core/log.hpp>

#include "imgui_convert.hpp"
#include "ui_resources.hpp"

namespace crimson::editor::ui
{
    static ImFont* s_regular = nullptr;
    static ImFont* s_bold = nullptr;

    // The bytes are static, so the atlas must not try to free them
    static ImFont* AddEmbeddedFont(const char* path, const ImFontConfig& base)
    {
        const EmbeddedFile* file = FindEmbeddedFile(GetUiResources(), path);
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
    static ImFont* AddTextFont(const char* path)
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

    static void LoadFonts()
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

    static void ApplyColors(ImGuiStyle& style)
    {
        using namespace theme;

        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text]                  = ToImVec4(Text);
        colors[ImGuiCol_TextDisabled]          = ToImVec4(TextMuted);
        colors[ImGuiCol_TextLink]              = ToImVec4(Text);
        colors[ImGuiCol_TextSelectedBg]        = ToImVec4(TextSelection);
        colors[ImGuiCol_InputTextCursor]       = ToImVec4(Text);

        colors[ImGuiCol_WindowBg]              = ToImVec4(PanelBackground);
        colors[ImGuiCol_ChildBg]               = ToImVec4(PanelBackground);
        colors[ImGuiCol_PopupBg]               = ToImVec4(PopupBackground);
        colors[ImGuiCol_Border]                = ToImVec4(Border);
        colors[ImGuiCol_BorderShadow]          = ToImVec4(WithAlpha(Border, 0.0f));

        colors[ImGuiCol_FrameBg]               = ToImVec4(FrameBackground);
        colors[ImGuiCol_FrameBgHovered]        = ToImVec4(Hover);
        colors[ImGuiCol_FrameBgActive]         = ToImVec4(Pressed);

        colors[ImGuiCol_TitleBg]               = ToImVec4(AppBackground);
        colors[ImGuiCol_TitleBgActive]         = ToImVec4(AppBackground);
        colors[ImGuiCol_TitleBgCollapsed]      = ToImVec4(AppBackground);
        colors[ImGuiCol_MenuBarBg]             = ToImVec4(AppBackground);

        colors[ImGuiCol_ScrollbarBg]           = ToImVec4(PanelBackground);
        colors[ImGuiCol_ScrollbarGrab]         = ToImVec4(FrameBackground);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ToImVec4(Hover);
        colors[ImGuiCol_ScrollbarGrabActive]   = ToImVec4(Pressed);

        colors[ImGuiCol_CheckMark]             = ToImVec4(Text);
        colors[ImGuiCol_CheckboxSelectedBg]    = ToImVec4(FrameBackground);
        colors[ImGuiCol_SliderGrab]            = ToImVec4(TextMuted);
        colors[ImGuiCol_SliderGrabActive]      = ToImVec4(Text);

        colors[ImGuiCol_Button]                = ToImVec4(FrameBackground);
        colors[ImGuiCol_ButtonHovered]         = ToImVec4(Hover);
        colors[ImGuiCol_ButtonActive]          = ToImVec4(Pressed);

        // Selected rows, tiles and menu items
        colors[ImGuiCol_Header]                = ToImVec4(Selection);
        colors[ImGuiCol_HeaderHovered]         = ToImVec4(Hover);
        colors[ImGuiCol_HeaderActive]          = ToImVec4(Pressed);

        colors[ImGuiCol_Separator]             = ToImVec4(Border);
        colors[ImGuiCol_SeparatorHovered]      = ToImVec4(Hover);
        colors[ImGuiCol_SeparatorActive]       = ToImVec4(Pressed);

        colors[ImGuiCol_ResizeGrip]            = ToImVec4(WithAlpha(Hover, 0.0f));
        colors[ImGuiCol_ResizeGripHovered]     = ToImVec4(Hover);
        colors[ImGuiCol_ResizeGripActive]      = ToImVec4(Pressed);

        // The selected tab is the panel's own color, so it joins the panel, under a thin accent line
        colors[ImGuiCol_Tab]                       = ToImVec4(AppBackground);
        colors[ImGuiCol_TabHovered]                = ToImVec4(Hover);
        colors[ImGuiCol_TabSelected]               = ToImVec4(PanelBackground);
        colors[ImGuiCol_TabSelectedOverline]       = ToImVec4(Accent);
        colors[ImGuiCol_TabDimmed]                 = ToImVec4(AppBackground);
        colors[ImGuiCol_TabDimmedSelected]         = ToImVec4(PanelBackground);
        colors[ImGuiCol_TabDimmedSelectedOverline] = ToImVec4(Border);
        colors[ImGuiCol_UnsavedMarker]             = ToImVec4(Text);

        colors[ImGuiCol_DockingPreview]        = ToImVec4(WithAlpha(Accent, 0.50f));
        colors[ImGuiCol_DockingEmptyBg]        = ToImVec4(AppBackground);

        colors[ImGuiCol_PlotLines]             = ToImVec4(TextMuted);
        colors[ImGuiCol_PlotLinesHovered]      = ToImVec4(Text);
        colors[ImGuiCol_PlotHistogram]         = ToImVec4(TextMuted);
        colors[ImGuiCol_PlotHistogramHovered]  = ToImVec4(Warning);

        colors[ImGuiCol_TableHeaderBg]         = ToImVec4(FrameBackground);
        colors[ImGuiCol_TableBorderStrong]     = ToImVec4(Border);
        colors[ImGuiCol_TableBorderLight]      = ToImVec4(Border);
        colors[ImGuiCol_TableRowBg]            = ToImVec4(WithAlpha(Text, 0.0f));
        colors[ImGuiCol_TableRowBgAlt]         = ToImVec4(WithAlpha(Text, 0.03f));
        colors[ImGuiCol_TreeLines]             = ToImVec4(Border);

        // A drop target is where the pointer is about to act, so it is drawn like focus
        colors[ImGuiCol_DragDropTarget]        = ToImVec4(Accent);
        colors[ImGuiCol_DragDropTargetBg]      = ToImVec4(WithAlpha(Accent, 0.15f));

        colors[ImGuiCol_NavCursor]             = ToImVec4(Accent);
        colors[ImGuiCol_NavWindowingHighlight] = ToImVec4(WithAlpha(Text, 0.70f));
        colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.30f);
        colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
    }

    namespace theme
    {
        void Apply(float scale)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            LoadFonts();

            ImGuiStyle& style = ImGui::GetStyle();

            style.WindowRounding         = Radius;
            style.ChildRounding          = Radius;
            style.FrameRounding          = Radius;
            style.PopupRounding          = Radius;
            style.GrabRounding           = Radius;
            style.ScrollbarRounding      = Radius;
            style.TabRounding            = Radius;
            style.DragDropTargetRounding = 0.0f; // a highlight takes the shape of what it is on, so rounded targets draw their own

            style.WindowPadding     = ImVec2(SpaceS, SpaceS);
            style.FramePadding      = ImVec2(SpaceS, SpaceXS);
            style.ItemSpacing       = ImVec2(SpaceS, SpaceXS);
            style.ItemInnerSpacing  = ImVec2(SpaceXS, SpaceXS);
            style.IndentSpacing     = SpaceL;
            style.ScrollbarSize     = SpaceM;
            style.GrabMinSize       = SpaceS;

            style.WindowBorderSize     = BorderSize;
            style.ChildBorderSize      = BorderSize;
            style.PopupBorderSize      = BorderSize;
            style.FrameBorderSize      = 0.0f;
            style.TabBorderSize        = 0.0f;
            style.TabBarBorderSize     = BorderSize;
            style.TabBarOverlineSize   = AccentLineSize;
            style.DockingSeparatorSize = BorderSize;

            style.FontSizeBase = FontBody;

            ApplyColors(style);

            // The fonts are drawn at the bigger size rather than stretched, so they stay sharp
            scale = std::clamp(scale, 0.5f, 4.0f);
            if (scale != 1.0f)
            {
                style.ScaleAllSizes(scale);
                style.FontScaleDpi = scale;
            }
        }

        float Scaled(float size) { return size * ImGui::GetStyle().FontScaleDpi; }

        ImFont* FontOf(Font font) { return font == Font::Bold ? s_bold : s_regular; }
    }
}
