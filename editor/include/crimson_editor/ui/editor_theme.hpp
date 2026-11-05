#pragma once
#include <imgui.h>

#include <crimson/asset_manager/asset_types.hpp>

namespace crimson::editor::ui
{
    // Sets the ImGui flags, fonts and colors. Call once, before the first ImGui frame. The scale is the monitor's
    // content scale, 1 on a screen at 100%.
    void ApplyEditorTheme(float scale = 1.0f);

    // The editor's colors and sizes, in one place so the panels agree. Colors are in ImGui's sRGB.
    namespace theme
    {
        inline constexpr ImVec4 Accent       = ImVec4(0.75f, 0.20f, 0.25f, 1.00f);
        inline constexpr ImVec4 AccentHover  = ImVec4(0.85f, 0.28f, 0.32f, 1.00f);
        inline constexpr ImVec4 AccentActive = ImVec4(0.65f, 0.15f, 0.20f, 1.00f);
        inline constexpr ImVec4 AccentSubtle = ImVec4(0.75f, 0.20f, 0.25f, 0.35f);

        inline constexpr ImVec4 Text      = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        inline constexpr ImVec4 TextMuted = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        inline constexpr ImVec4 Warning   = ImVec4(1.00f, 0.75f, 0.20f, 1.00f);
        inline constexpr ImVec4 Error     = ImVec4(1.00f, 0.35f, 0.35f, 1.00f);
        inline constexpr ImVec4 Success   = ImVec4(0.40f, 0.80f, 0.45f, 1.00f);

        // Buttons that switch something on and off, in the Console's filters for example
        inline constexpr ImVec4 ToggleOff     = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        inline constexpr ImVec4 ToggleInfo    = ImVec4(0.20f, 0.40f, 0.20f, 1.00f);
        inline constexpr ImVec4 ToggleWarning = ImVec4(0.50f, 0.40f, 0.10f, 1.00f);
        inline constexpr ImVec4 ToggleError   = ImVec4(0.50f, 0.10f, 0.10f, 1.00f);

        inline constexpr ImVec4 Folder    = ImVec4(0.67f, 0.55f, 0.27f, 1.00f);
        inline constexpr ImVec4 FolderTab = ImVec4(0.59f, 0.47f, 0.22f, 1.00f);

        // Behind images and render targets, where nothing is drawn yet
        inline constexpr ImVec4 Backdrop = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);

        // What tells the kinds of assets apart in the Assets panel
        ImVec4 AssetColor(AssetType type);

        // Secondary text, such as hints, is drawn at this fraction of the font size
        inline constexpr float SmallTextScale = 0.85f;

        ImFont* RegularFont();
        ImFont* BoldFont();

        // Bold text for one scope, such as a section title
        struct ScopedBold
        {
            ScopedBold() { ImGui::PushFont(BoldFont(), 0.0f); }
            ~ScopedBold() { ImGui::PopFont(); }
            ScopedBold(const ScopedBold&) = delete;
            ScopedBold& operator=(const ScopedBold&) = delete;
        };

        // A muted line of text for when a panel has nothing to show yet, centered in what is left of it
        void EmptyHint(const char* text);

        // A bold title on a separator line, above a group of fields
        inline void SectionTitle(const char* text)
        {
            const ScopedBold bold;
            ImGui::SeparatorText(text);
        }
    }
}
