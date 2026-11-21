# The editor's look

The editor's colors, sizes and fonts are named in [`editor_theme.hpp`](../editor/ui/include/crimson_editor/ui/editor_theme.hpp). A panel uses those names and writes no color, spacing, radius or font size of its own. `tests/editor/test_editor_theme.cpp` checks the panels' sources for it.

## References

| Editor | What the Crimson editor takes from it |
| --- | --- |
| **Unity 6** | Inspector rows and component headers, and a muted color for each kind of asset |
| **Godot 4** | A few levels of surface, the accent kept for focus and selection, and flat tabs |
| **Blender** | Dense spacing, 1 px splitters, and regions told apart by contrast rather than by lines |
| **JetBrains IDEs, New UI** | A 13 to 14 px body, grey hovers, an underline on the active tab, and monochrome muted icons |

## Rules

- **Hovers and presses are grey** (`Hover`, `Pressed`), never the accent.
- **The accent** (`Accent` and its shades) marks selection, focus, the active tab's line and primary buttons, and nothing else. Drag and drop targets and the docking preview are focus, so they use it too.
- **One radius** (`Radius`) for every corner, and 1 px (`BorderSize`) for borders and splitters.
- **Spacing comes from the scale**: `SpaceXS`, `SpaceS`, `SpaceM`, `SpaceL`, `SpaceXL`. Widths that depend on the text, such as a field that fits 12 characters, are in ems: `ui::FontSize() * 12.0f`. They are not spacing, so they have no token.
- **Three type sizes and two weights**: `FontBody`, `FontSmall` and `FontHeading`, in Regular and Bold.
- **One icon size** (`IconSize`, the body size). An icon is muted unless it carries meaning: a status, a kind of asset or an axis.
- **Sliders always have a number field next to them** (`props::Slider`, or `props::SliderField` outside a props table), so a value can be typed as well as dragged.
- **Surfaces** go back to front as `AppBackground` (the dock space, tab bars and menu bar), `PanelBackground`, `FrameBackground` (fields, buttons and component headers) and `PopupBackground` (popups, menus and notifications).

## Tokens

| Group | Names |
| --- | --- |
| Surfaces | `AppBackground`, `PanelBackground`, `FrameBackground`, `PopupBackground`, `Border` |
| Interaction | `Hover`, `Pressed` |
| Text | `Text`, `TextMuted`, `TextOnAccent` |
| Accent | `Accent`, `AccentHover`, `AccentPressed`, `Selection`, `TextSelection` |
| Status | `Success`, `Warning`, `Error`, with `TintAlpha`, `TintHoverAlpha` and `WithAlpha` for tinted controls |
| Category | `AssetMaterial`, `AssetMesh`, `AssetShader`, `AssetTexture`, `AssetScene`, `AssetOther`, `Folder`, `FolderTab`, and `AxisX` to `AxisW` |
| Spacing | `SpaceXS` 4, `SpaceS` 8, `SpaceM` 12, `SpaceL` 16, `SpaceXL` 24 |
| Shape | `Radius` 4, `BorderSize` 1, `AccentLineSize` 2 |
| Type | `FontBody` 14, `FontSmall` 12, `FontHeading` 16, `IconSize` 14 |

Sizes are at 100%. The style scales them with the monitor, and `Scaled` does the same for a token used outside the style, such as a margin.

## Fonts

Inter stays, in Regular and Bold, at its 18 pt optical size, the smallest that is shipped. It is drawn at 14 px. The fonts are rasterised at the monitor's scale rather than stretched, so text stays sharp at 150% and 200%. The Lucide icons are merged into both.

## Helpers

- `ui::PrimaryButton` for the main action of a dialog or a form. Only one per dialog.
- `ui::ToggleButton` for a button that stays pressed while its setting is on, such as a gizmo tool.
- `ui::TreeRow` for a tree row with an icon, which draws the icon in its own color: `TextMuted` for entities and components, `Folder` for folders.
- `ui::theme::Scaled` for a spacing, radius or margin token that goes outside the style, such as a painter's rounding.
- `ui::FontScope` for one scope in another font or type size, `ui::BoldScope` for bold at the current size.
- `ui::TintedToggle` for a toggle that takes the color of a status while it is on, such as the Console's level buttons.
- `ui::Painter` for what is drawn by hand, such as a thumbnail or a highlight. It takes only `ui::Color`s, so a panel has no ImGui types to write.
- Nothing else: a panel includes only `crimson_editor/ui/`, whose headers hold no ImGui. [architecture.md](architecture.md#the-ui-layer) says why.
- `ui::WithAlpha` for a status color as a tint.
- `AssetField` for any reference to an asset, never a combo. Its picker marks the engine's assets with a `TextMuted` name and an `Engine` label, and the field has the accent border while its picker is open.

## Next

The toolbar and status bar (#114) and the empty, loading and error states (#115) take their surfaces and status colors from these tokens.
