#include "crimson_editor/panels/asset_field.hpp"

#include <algorithm>
#include <cmath>

#include "crimson_editor/panels/asset_drag.hpp"
#include "crimson_editor/panels/asset_look.hpp"
#include "crimson_editor/panels/asset_tiles.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/painter.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
{
    std::string AssetFieldText(const AssetManager& assets, AssetID id, AssetType type)
    {
        if (const AssetInfo* info = assets.GetInfo(id))
            return AssetFileName(info->Path);

        if (const MissingAssetRef* missing = assets.GetMissing(id))
            return "Missing: " + (missing->Path.empty() ? id.ToString() : missing->Path);

        return id ? "Missing" : "None (" + std::string(type.Name()) + ")";
    }

    bool AssetField(const char* id, EditorContext& context, AssetType type, AssetID& current, AssetID whenEmpty)
    {
        const AssetManager& assets = context.Session.GetAssets();

        const ui::WidgetId owner = ui::IdOf(id);
        const float height = ui::FrameHeight();
        const float rounding = ui::FrameRounding();

        // One frame holds the icon, the name and the picker button, which is drawn over its right end. The frame is the
        // drop target, so its highlight covers all of it.
        const bool frameClicked = ui::HitArea(id, glm::vec2(std::max(ui::ItemWidth(), height * 2.0f), height), true);
        const bool frameHovered = ui::item::Hovered();
        const ui::Rect frame = ui::item::Bounds();

        const AssetID dropped = asset_drag::AcceptOnLastItem(assets, type, ui::drag_drop::DropLook::None);

        const float nameRight = frame.Max.x - height;
        const bool overName = ui::input::MousePosition().x < nameRight;
        const bool hovered = frameHovered && overName;

        // What it shows, which is what an empty one uses if it says so. Choices are compared with what is stored.
        const bool showsFallback = !current && whenEmpty;
        const AssetID shown = showsFallback ? whenEmpty : current;

        const AssetInfo* info = assets.GetInfo(shown);
        const bool missing = assets.IsMissing(shown);

        if (frameClicked && overName && shown)
            context.ShowAsset(shown);

        if (hovered)
        {
            if (missing)
                ui::Tooltip("The file isn't there. The reference is kept, and works again once it is back.", ui::TooltipDelay::None);
            else if (showsFallback)
                ui::Tooltip("Empty, so it uses " + (info ? info->Path : std::string("nothing")), ui::TooltipDelay::None);
            else if (info)
                ui::Tooltip(info->Path, ui::TooltipDelay::None);
        }

        ui::Painter painter;
        painter.FillRect(frame, hovered ? ui::theme::Hover : ui::theme::FrameBackground, rounding);

        if (context.Picker.IsOpenFor(owner))
            painter.StrokeRect(frame, ui::theme::Accent, rounding, ui::theme::Scaled(ui::theme::BorderSize));

        // The picker button, a square at the right end whose icon is centered by what it draws
        const ui::Rect button{ glm::vec2(nameRight, frame.Min.y), frame.Max };

        ui::SetCursor(button.Min);
        const bool pickerClicked = [&]
        {
            const ui::IdScope scope(id);
            return ui::HitArea("##Pick", glm::vec2(height, height));
        }();

        if (pickerClicked)
            context.Picker.Open(owner, type, shown, glm::vec2(frame.Min.x, frame.Max.y));

        if (ui::item::Hovered())
            painter.FillRect(button, ui::item::Active() ? ui::theme::Pressed : ui::theme::Hover, rounding, ui::Corners::Right);

        // Icons are as big as the text, in a box of that size at the middle of their square
        const float iconSize = ui::FontSize();
        const auto drawIcon = [&](float left, const char* icon, ui::Color color)
        {
            const glm::vec2 center(left + height * 0.5f, frame.Min.y + height * 0.5f);
            painter.Icon(ui::Rect{ center - glm::vec2(iconSize * 0.5f), center + glm::vec2(iconSize * 0.5f) }, icon, color);
        };

        drawIcon(nameRight, ui::icons::kPicker, ui::theme::Text);

        // Not for a missing one, as the empty thumbnail would be kept after the file is back
        const TextureHandle picture = type.Is<TextureAsset>() && info ? context.Thumbnails.Get(assets.As<TextureAsset>(shown)) : TextureHandle{};

        if (picture)
        {
            const float inset = std::round(ui::FramePadding().y * 0.5f);
            const float side = height - inset * 2.0f;
            painter.Image(picture, ui::Rect{ frame.Min + glm::vec2(inset), frame.Min + glm::vec2(inset + side) });
        }
        else
        {
            drawIcon(frame.Min.x, AssetIcon(type), AssetColor(type));
        }

        const ui::Color textColor = missing ? ui::theme::Warning : showsFallback || (info && info->ReadOnly) ? ui::theme::TextMuted : ui::theme::Text;
        const std::string text = AssetFieldText(assets, shown, type);

        {
            const ui::ClipScope clip(ui::Rect{ frame.Min, glm::vec2(nameRight, frame.Max.y) });
            painter.Text(glm::vec2(frame.Min.x + height, frame.Min.y + ui::FramePadding().y), textColor, text);
        }

        asset_drag::DrawDropHighlight(assets, type, frame, rounding);

        bool changed = false;

        if (dropped && dropped != current)
        {
            current = dropped;
            changed = true;
        }

        if (const std::optional<AssetID> picked = context.Picker.TakeChoice(owner); picked && *picked != current)
        {
            current = *picked;
            changed = true;
        }

        return changed;
    }
}
