#pragma once
#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    // The two weights of the editor's font
    enum class Font
    {
        Regular,
        Bold
    };

    // The editor's colors and sizes, in one place so the panels agree. Colors are in sRGB, sizes are in pixels at 100%.
    // docs/ui.md has the rules for using them.
    namespace theme
    {
        // Surfaces, from the back to the front. They share one slight cool tint, so they read as one family of greys.
        inline constexpr Color AppBackground   = { 0.095f, 0.100f, 0.110f, 1.00f };
        inline constexpr Color PanelBackground = { 0.135f, 0.140f, 0.150f, 1.00f };
        inline constexpr Color FrameBackground = { 0.190f, 0.195f, 0.210f, 1.00f };
        inline constexpr Color PopupBackground = { 0.165f, 0.170f, 0.185f, 1.00f };

        // What the pointer does to a surface is told with a lighter grey, never with the accent
        inline constexpr Color Hover   = { 0.250f, 0.255f, 0.270f, 1.00f };
        inline constexpr Color Pressed = { 0.300f, 0.305f, 0.320f, 1.00f };
        inline constexpr Color Border  = { 0.070f, 0.070f, 0.080f, 1.00f };

        inline constexpr Color Text         = { 0.92f, 0.92f, 0.92f, 1.00f };
        inline constexpr Color TextMuted    = { 0.50f, 0.50f, 0.50f, 1.00f };
        inline constexpr Color TextOnAccent = { 0.98f, 0.98f, 0.98f, 1.00f };

        // The one accent. It marks selection, focus, the active tab's line and primary buttons, and nothing else.
        inline constexpr Color Accent        = { 0.75f, 0.20f, 0.25f, 1.00f };
        inline constexpr Color AccentHover   = { 0.85f, 0.28f, 0.32f, 1.00f };
        inline constexpr Color AccentPressed = { 0.65f, 0.15f, 0.20f, 1.00f };
        inline constexpr Color Selection     = { 0.40f, 0.15f, 0.18f, 1.00f };
        inline constexpr Color TextSelection = { 0.75f, 0.20f, 0.25f, 0.35f };

        inline constexpr Color Warning = { 1.00f, 0.75f, 0.20f, 1.00f };
        inline constexpr Color Error   = { 1.00f, 0.35f, 0.35f, 1.00f };
        inline constexpr Color Success = { 0.40f, 0.80f, 0.45f, 1.00f };

        // Controls tinted with a status color are drawn at these alphas, without and with the pointer over them
        inline constexpr float TintAlpha      = 0.25f;
        inline constexpr float TintHoverAlpha = 0.40f;

        // Muted and of similar lightness, so no kind of asset shouts over the others
        inline constexpr Color Folder    = { 0.68f, 0.60f, 0.40f, 1.00f };
        inline constexpr Color FolderTab = { 0.58f, 0.50f, 0.33f, 1.00f };

        // The stripes on X, Y, Z and W fields, in the gizmo's colors
        inline constexpr Color AxisX = { 0.80f, 0.27f, 0.27f, 1.00f };
        inline constexpr Color AxisY = { 0.35f, 0.65f, 0.27f, 1.00f };
        inline constexpr Color AxisZ = { 0.27f, 0.45f, 0.85f, 1.00f };
        inline constexpr Color AxisW = { 0.45f, 0.45f, 0.48f, 1.00f };

        // Behind images and render targets, where nothing is drawn yet
        inline constexpr Color Backdrop = { 0.00f, 0.00f, 0.00f, 1.00f };

        // What tells the kinds of assets apart in the Assets panel
        inline constexpr Color AssetMaterial = { 0.66f, 0.42f, 0.52f, 1.00f };
        inline constexpr Color AssetMesh     = { 0.38f, 0.54f, 0.68f, 1.00f };
        inline constexpr Color AssetShader   = { 0.52f, 0.46f, 0.70f, 1.00f };
        inline constexpr Color AssetTexture  = { 0.40f, 0.62f, 0.48f, 1.00f };
        inline constexpr Color AssetScene    = { 0.70f, 0.50f, 0.36f, 1.00f };
        inline constexpr Color AssetOther    = { 0.42f, 0.43f, 0.46f, 1.00f };

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

        // A size from the scales above at the monitor's scale, for what the style does not scale by itself
        [[nodiscard]] float Scaled(float size);
    }
}
