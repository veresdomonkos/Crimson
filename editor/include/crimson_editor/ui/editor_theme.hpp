#pragma once
#include <imgui.h>

#include <crimson/asset_manager/asset_types.hpp>

namespace crimson::editor::ui
{
    // Sets the ImGui flags, fonts and colors. Call once, before the first ImGui frame. The scale is the monitor's
    // content scale, 1 on a screen at 100%.
    void ApplyEditorTheme(float scale = 1.0f);

    // The editor's colors and sizes, in one place so the panels agree. Colors are in ImGui's sRGB, sizes are in pixels
    // at 100%. docs/ui.md has the rules for using them.
    namespace theme
    {
        // Surfaces, from the back to the front. They share one slight cool tint, so they read as one family of greys.
        inline constexpr ImVec4 AppBackground   = ImVec4(0.095f, 0.100f, 0.110f, 1.00f);
        inline constexpr ImVec4 PanelBackground = ImVec4(0.135f, 0.140f, 0.150f, 1.00f);
        inline constexpr ImVec4 FrameBackground = ImVec4(0.190f, 0.195f, 0.210f, 1.00f);
        inline constexpr ImVec4 PopupBackground = ImVec4(0.165f, 0.170f, 0.185f, 1.00f);

        // What the pointer does to a surface is told with a lighter grey, never with the accent
        inline constexpr ImVec4 Hover   = ImVec4(0.250f, 0.255f, 0.270f, 1.00f);
        inline constexpr ImVec4 Pressed = ImVec4(0.300f, 0.305f, 0.320f, 1.00f);
        inline constexpr ImVec4 Border  = ImVec4(0.070f, 0.070f, 0.080f, 1.00f);

        inline constexpr ImVec4 Text         = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        inline constexpr ImVec4 TextMuted    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        inline constexpr ImVec4 TextOnAccent = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);

        // The one accent. It marks selection, focus, the active tab's line and primary buttons, and nothing else.
        inline constexpr ImVec4 Accent        = ImVec4(0.75f, 0.20f, 0.25f, 1.00f);
        inline constexpr ImVec4 AccentHover   = ImVec4(0.85f, 0.28f, 0.32f, 1.00f);
        inline constexpr ImVec4 AccentPressed = ImVec4(0.65f, 0.15f, 0.20f, 1.00f);
        inline constexpr ImVec4 Selection     = ImVec4(0.40f, 0.15f, 0.18f, 1.00f);
        inline constexpr ImVec4 TextSelection = ImVec4(0.75f, 0.20f, 0.25f, 0.35f);

        inline constexpr ImVec4 Warning = ImVec4(1.00f, 0.75f, 0.20f, 1.00f);
        inline constexpr ImVec4 Error   = ImVec4(1.00f, 0.35f, 0.35f, 1.00f);
        inline constexpr ImVec4 Success = ImVec4(0.40f, 0.80f, 0.45f, 1.00f);

        // Controls tinted with a status color are drawn at these alphas, without and with the pointer over them
        inline constexpr float TintAlpha      = 0.25f;
        inline constexpr float TintHoverAlpha = 0.40f;

        constexpr ImVec4 WithAlpha(const ImVec4& color, float alpha)
        {
            return ImVec4(color.x, color.y, color.z, alpha);
        }

        // Muted and of similar lightness, so no kind of asset shouts over the others
        inline constexpr ImVec4 Folder    = ImVec4(0.68f, 0.60f, 0.40f, 1.00f);
        inline constexpr ImVec4 FolderTab = ImVec4(0.58f, 0.50f, 0.33f, 1.00f);

        // The stripes on X, Y, Z and W fields, in the gizmo's colors
        inline constexpr ImVec4 AxisX = ImVec4(0.80f, 0.27f, 0.27f, 1.00f);
        inline constexpr ImVec4 AxisY = ImVec4(0.35f, 0.65f, 0.27f, 1.00f);
        inline constexpr ImVec4 AxisZ = ImVec4(0.27f, 0.45f, 0.85f, 1.00f);
        inline constexpr ImVec4 AxisW = ImVec4(0.45f, 0.45f, 0.48f, 1.00f);

        // Behind images and render targets, where nothing is drawn yet
        inline constexpr ImVec4 Backdrop = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);

        // What tells the kinds of assets apart in the Assets panel
        ImVec4 AssetColor(AssetType type);

        // The spacing scale, and the one corner radius and border size
        inline constexpr float SpaceXS = 4.0f;
        inline constexpr float SpaceS  = 8.0f;
        inline constexpr float SpaceM  = 12.0f;
        inline constexpr float SpaceL  = 16.0f;
        inline constexpr float SpaceXL = 24.0f;

        inline constexpr float Radius         = 4.0f;
        inline constexpr float BorderSize     = 1.0f;
        inline constexpr float AccentLineSize = 2.0f;

        // The type scale, in two weights. Icons are drawn at the body size.
        inline constexpr float FontBody    = 14.0f;
        inline constexpr float FontSmall   = 12.0f;
        inline constexpr float FontHeading = 16.0f;
        inline constexpr float IconSize    = FontBody;

        ImFont* RegularFont();
        ImFont* BoldFont();

        // A size from the scales above at the monitor's scale, for what the style does not scale by itself
        float Scaled(float size);

        // A font and size for one scope. The size is a type token, before the monitor's scale, and 0 keeps the current one.
        struct ScopedFont
        {
            ScopedFont(ImFont* font, float size) { ImGui::PushFont(font, size); }
            ~ScopedFont() { ImGui::PopFont(); }
            ScopedFont(const ScopedFont&) = delete;
            ScopedFont& operator=(const ScopedFont&) = delete;
        };

        // Bold text for one scope, such as a section title
        struct ScopedBold
        {
            ScopedBold() { ImGui::PushFont(BoldFont(), 0.0f); }
            ~ScopedBold() { ImGui::PopFont(); }
            ScopedBold(const ScopedBold&) = delete;
            ScopedBold& operator=(const ScopedBold&) = delete;
        };

        // The main action of a dialog or a form. The only button with the accent.
        bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

        // A button that stays pressed while its setting is on
        bool ToggleButton(const char* label, bool on, const ImVec2& size = ImVec2(0.0f, 0.0f));

        // A tree node whose icon has a color of its own, which one label can't have. The node is drawn without a label,
        // and the icon and the name are drawn over it, where the label would be. It needs ImGuiTreeNodeFlags_SpanAvailWidth
        // or _Framed, so the item covers the row. An empty label draws just the icon.
        bool IconTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* icon, const ImVec4& iconColor, const char* label);

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
