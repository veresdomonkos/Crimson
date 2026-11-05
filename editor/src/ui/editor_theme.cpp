#include "crimson_editor/ui/editor_theme.hpp"

#include <algorithm>

#include <imgui.h>

#include <crimson/core/log.hpp>

#include "crimson_editor/editor_resources.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        constexpr float kFontSize = 18.0f;

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
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(file->Data.data()), static_cast<int>(file->Data.size()), kFontSize, &config);
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
            icons.GlyphMinAdvanceX = kFontSize;
            icons.GlyphOffset = ImVec2(0.0f, 2.0f);
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
            colors[ImGuiCol_WindowBg]              = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
            colors[ImGuiCol_ChildBg]               = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
            colors[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
            colors[ImGuiCol_Border]                = ImVec4(0.25f, 0.25f, 0.27f, 0.60f);
            colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

            colors[ImGuiCol_FrameBg]               = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.26f, 0.27f, 0.29f, 1.00f);
            colors[ImGuiCol_FrameBgActive]         = ImVec4(0.30f, 0.31f, 0.33f, 1.00f);

            colors[ImGuiCol_TitleBg]               = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
            colors[ImGuiCol_TitleBgActive]         = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.11f, 0.11f, 0.12f, 0.75f);

            colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

            colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.38f, 0.38f, 0.41f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.46f, 0.46f, 0.50f, 1.00f);

            const auto accentAlpha = [](float alpha) { return ImVec4(Accent.x, Accent.y, Accent.z, alpha); };

            colors[ImGuiCol_CheckMark]             = Accent;
            colors[ImGuiCol_SliderGrab]            = Accent;
            colors[ImGuiCol_SliderGrabActive]      = AccentHover;

            colors[ImGuiCol_Button]                = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
            colors[ImGuiCol_ButtonHovered]         = Accent;
            colors[ImGuiCol_ButtonActive]          = AccentActive;

            colors[ImGuiCol_Header]                = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
            colors[ImGuiCol_HeaderHovered]         = accentAlpha(0.55f);
            colors[ImGuiCol_HeaderActive]          = Accent;

            colors[ImGuiCol_Separator]             = colors[ImGuiCol_Border];
            colors[ImGuiCol_SeparatorHovered]      = accentAlpha(0.65f);
            colors[ImGuiCol_SeparatorActive]       = Accent;

            colors[ImGuiCol_ResizeGrip]            = accentAlpha(0.20f);
            colors[ImGuiCol_ResizeGripHovered]     = accentAlpha(0.55f);
            colors[ImGuiCol_ResizeGripActive]      = Accent;

            colors[ImGuiCol_Tab]                   = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
            colors[ImGuiCol_TabHovered]            = accentAlpha(0.60f);
            colors[ImGuiCol_TabActive]             = ImVec4(0.55f, 0.18f, 0.22f, 1.00f);
            colors[ImGuiCol_TabUnfocused]          = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);

            colors[ImGuiCol_TabSelectedOverline]       = Accent;
            colors[ImGuiCol_TabDimmedSelectedOverline] = accentAlpha(0.50f);

            colors[ImGuiCol_DockingPreview]        = accentAlpha(0.60f);
            colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);

            colors[ImGuiCol_PlotLines]             = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
            colors[ImGuiCol_PlotLinesHovered]      = AccentHover;
            colors[ImGuiCol_PlotHistogram]         = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
            colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

            colors[ImGuiCol_TextSelectedBg]        = AccentSubtle;
            colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);

            colors[ImGuiCol_NavHighlight]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
            colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
            colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
        }
    }

    void ApplyEditorTheme(float scale)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        LoadFonts();

        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding    = 4.0f;
        style.FrameRounding     = 3.0f;
        style.GrabRounding      = 3.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.TabRounding       = 3.0f;

        style.WindowPadding     = ImVec2(10.0f, 10.0f);
        style.FramePadding      = ImVec2(8.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
        style.IndentSpacing     = 20.0f;
        style.ScrollbarSize     = 14.0f;
        style.GrabMinSize       = 10.0f;

        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.PopupBorderSize   = 1.0f;

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
                case AssetType::Material: return ImVec4(0.59f, 0.24f, 0.27f, 1.00f);
                case AssetType::Mesh:     return ImVec4(0.24f, 0.43f, 0.59f, 1.00f);
                case AssetType::Shader:   return ImVec4(0.37f, 0.29f, 0.59f, 1.00f);
                case AssetType::Texture:  return ImVec4(0.24f, 0.51f, 0.35f, 1.00f);
                case AssetType::Scene:    return ImVec4(0.67f, 0.41f, 0.20f, 1.00f);
                default:                  return ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
            }
        }

        ImFont* RegularFont() { return s_regular; }
        ImFont* BoldFont() { return s_bold; }

        void EmptyHint(const char* text)
        {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * SmallTextScale);

            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float width = std::min(ImGui::CalcTextSize(text).x, available.x);
            const ImVec2 cursor = ImGui::GetCursorPos();

            ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0f, (available.x - width) * 0.5f), cursor.y + ImGui::GetStyle().ItemSpacing.y * 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            ImGui::PopFont();
        }
    }
}
