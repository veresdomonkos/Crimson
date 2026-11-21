#include "crimson_editor/panels/asset_picker.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "crimson_editor/asset_thumbnails.hpp"
#include "crimson_editor/panels/asset_look.hpp"
#include "crimson_editor/panels/asset_tiles.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/menus.hpp"
#include "crimson_editor/ui/painter.hpp"
#include "crimson_editor/ui/widgets.hpp"
#include "crimson_editor/ui/window.hpp"

namespace crimson::editor
{
    constexpr const char* kPopupId = "##AssetPicker";
    constexpr int kGridColumns = 4;

    // The window is sized in ems, so the popup keeps its size while the entries change
    constexpr float kBodyWidthEms = 22.0f;
    constexpr float kBodyHeightEms = 24.0f;

    static std::string Lowered(std::string_view text)
    {
        std::string lowered(text);
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

    static int IndexOf(const std::vector<PickerEntry>& entries, AssetID id)
    {
        const auto it = std::ranges::find(entries, id, &PickerEntry::Id);
        return it == entries.end() ? -1 : static_cast<int>(it - entries.begin());
    }

    namespace asset_picker
    {
        std::vector<PickerEntry> Entries(std::span<const AssetInfo* const> assets, std::string_view search)
        {
            const std::string needle = Lowered(search);

            std::vector<PickerEntry> entries;
            for (const AssetInfo* info : assets)
            {
                if (IsInternal(*info))
                    continue;

                std::string name = AssetFileName(info->Path);
                if (!needle.empty() && !Lowered(name).contains(needle))
                    continue;

                entries.push_back({ info->Id, std::move(name), info->Path, info->ReadOnly });
            }

            std::ranges::stable_sort(entries, [](const PickerEntry& a, const PickerEntry& b)
            {
                if (a.Engine != b.Engine)
                    return !a.Engine;

                return Lowered(a.Name) < Lowered(b.Name);
            });

            entries.insert(entries.begin(), PickerEntry{ .Name = "None" });
            return entries;
        }

        int MoveSelection(int index, int count, int dx, int dy, int columns)
        {
            if (count <= 0)
                return -1;

            if (index < 0)
                return 0;

            return std::clamp(index + dx + dy * columns, 0, count - 1);
        }
    }

    void AssetPicker::Open(ui::WidgetId owner, AssetType type, AssetID current, glm::vec2 below)
    {
        m_owner = owner;
        m_type = type;
        m_selected = current;
        m_original = current;
        m_pending.reset();
        m_changed = false;
        m_search.clear();
        m_open = true;
        m_fieldDrawn = true;
        m_justOpened = true;
        m_scrollToSelected = true;
        m_position = below;
    }

    void AssetPicker::Close()
    {
        m_open = false;
        m_justOpened = false;
    }

    void AssetPicker::Cancel()
    {
        if (m_changed)
        {
            m_selected = m_original;
            m_pending = m_original;
            m_changed = false;
        }

        Close();
    }

    void AssetPicker::Drop()
    {
        Close();
        m_pending.reset();
        m_changed = false;
        m_owner = 0;
    }

    void AssetPicker::Choose(AssetID id)
    {
        m_selected = id;
        m_pending = id;
        m_changed = true;
    }

    void AssetPicker::Confirm()
    {
        m_pending = m_selected;
        Close();
    }

    void AssetPicker::SetSearch(std::string_view search)
    {
        m_search = search;
    }

    std::optional<AssetID> AssetPicker::TakeChoice(ui::WidgetId owner)
    {
        if (m_owner != owner)
            return std::nullopt;

        // Asked by every field each frame it is drawn, which is how the picker knows its field is still there
        m_fieldDrawn = true;

        if (!m_pending)
            return std::nullopt;

        const std::optional<AssetID> choice = m_pending;
        m_pending.reset();
        return choice;
    }

    void AssetPicker::EndFrame()
    {
        // The field that opened it isn't drawn any more, so nothing is left to choose for
        if (!m_fieldDrawn && (m_open || m_pending))
            Drop();

        m_fieldDrawn = false;
    }

    void AssetPicker::Draw(const AssetManager& assets, AssetThumbnails& thumbnails)
    {
        EndFrame();

        // Before the popup, as it closes on Escape by itself
        if (m_open && !m_justOpened && ui::input::KeyPressed(Key::Escape))
            Cancel();

        if (m_justOpened)
        {
            ui::Popup::Open(kPopupId);
            m_justOpened = false;
        }

        if (!m_open && !ui::Popup::IsOpen(kPopupId))
            return;

        const float bodyWidth = std::round(ui::FontSize() * kBodyWidthEms);
        const float bodyHeight = std::round(ui::FontSize() * kBodyHeightEms);
        const glm::vec2 padding = ui::WindowPadding();
        const glm::vec2 spacing = ui::ItemSpacing();

        // Kept on screen by what the window takes: the body, the search row, the title and the padding
        const ui::Rect work = ui::WorkArea();
        const glm::vec2 size(bodyWidth + padding.x * 2.0f, bodyHeight + ui::FrameHeight() + ui::LineHeight() + spacing.y * 2.0f + padding.y * 2.0f);
        const glm::vec2 position(std::clamp(m_position.x, work.Min.x, std::max(work.Min.x, work.Max.x - size.x)),
                                 std::clamp(m_position.y, work.Min.y, std::max(work.Min.y, work.Max.y - size.y)));

        // Without navigation, as the arrow keys are the picker's own and would move off the search box
        ui::Popup popup(kPopupId, ui::PopupOptions{ .Position = position, .NoNavigation = true });
        if (!popup)
        {
            m_open = false;
            return;
        }

        if (!m_open)
        {
            popup.Close();
            return;
        }

        {
            const ui::BoldScope bold;
            ui::Text("Select " + std::string(m_type.Name()));
        }

        const float buttonWidth = ui::TextSize("Grid").x + ui::FramePadding().x * 2.0f;

        const std::string hint = std::string(ui::icons::kSearch) + " Search";
        ui::NextItemWidth(bodyWidth - buttonWidth * 2.0f - spacing.x * 2.0f);
        ui::InputText("##Search", m_search, ui::InputOptions{ .Hint = hint, .Focus = popup.IsAppearing() });
        const bool typing = ui::item::Active() && !m_search.empty();

        {
            const ui::Row row(true);

            if (ui::ToggleButton("List", m_view == View::List, glm::vec2(buttonWidth, 0.0f)))
                m_view = View::List;

            if (ui::ToggleButton("Grid", m_view == View::Grid, glm::vec2(buttonWidth, 0.0f)))
                m_view = View::Grid;
        }

        const std::vector<const AssetInfo*> listed = assets.List(m_type);
        const std::vector<PickerEntry> entries = asset_picker::Entries(listed, m_search);
        int selectedIndex = IndexOf(entries, m_selected);

        const bool grid = m_view == View::Grid;
        const int columns = grid ? kGridColumns : 1;

        // The left and right arrows move the cursor in the search box while there is something to move through
        int dx = 0;
        int dy = 0;
        if (ui::input::KeyPressed(Key::Up, true)) dy = -1;
        if (ui::input::KeyPressed(Key::Down, true)) dy = 1;
        if (grid && !typing && ui::input::KeyPressed(Key::Left, true)) dx = -1;
        if (grid && !typing && ui::input::KeyPressed(Key::Right, true)) dx = 1;

        if (dx != 0 || dy != 0)
        {
            if (const int moved = asset_picker::MoveSelection(selectedIndex, static_cast<int>(entries.size()), dx, dy, columns); moved >= 0)
            {
                Choose(entries[moved].Id);
                selectedIndex = moved;
                m_scrollToSelected = true;
            }
        }

        if (ui::input::KeyPressed(Key::Enter) || ui::input::KeyPressed(Key::KeypadEnter))
            Confirm();

        if (m_open)
            DrawEntries(assets, thumbnails, entries, selectedIndex);

        if (!m_open)
            popup.Close();
    }

    void AssetPicker::DrawEntries(const AssetManager& assets, AssetThumbnails& thumbnails, const std::vector<PickerEntry>& entries, int selectedIndex)
    {
        const float width = std::round(ui::FontSize() * kBodyWidthEms);
        const float height = std::round(ui::FontSize() * kBodyHeightEms);
        const bool grid = m_view == View::Grid;

        const float tileSize = std::floor((width - ui::ScrollbarSize() - ui::ItemSpacing().x * (kGridColumns - 1)) / kGridColumns);

        const ui::Region region("##Entries", glm::vec2(width, height));
        const ui::TileGrid tiles(tileSize);

        for (int index = 0; index < static_cast<int>(entries.size()); ++index)
        {
            const PickerEntry& entry = entries[index];
            const bool selected = index == selectedIndex;
            const ui::Color nameColor = entry.Engine ? ui::theme::TextMuted : ui::theme::Text;
            const char* icon = entry.Id ? AssetIcon(m_type) : nullptr; // None has none

            const ui::IdScope scope(index);
            ui::Painter painter;

            if (grid)
            {
                const ui::TileLayout tile = ui::Tile("##Tile", tileSize, selected);
                if (tile.Clicked)
                    Choose(entry.Id);

                if (ui::item::DoubleClicked())
                    Confirm();

                if (!entry.Path.empty())
                    ui::Tooltip(entry.Path, ui::TooltipDelay::None);

                // Only pictures that are on screen get loaded
                TextureHandle picture;
                if (m_type.Is<TextureAsset>() && entry.Id && ui::item::Visible())
                    picture = thumbnails.Get(assets.As<TextureAsset>(entry.Id));

                if (icon)
                    DrawAssetPicture(painter, tile.Icon, m_type, picture);

                painter.ClippedText(tile.LabelPosition, tile.LabelWidth, entry.Name, nameColor);
            }
            else
            {
                // Drawn rather than a Selectable, whose highlight is square and reaches past the row into the spacing, where
                // the child cuts it off at the top and on the left
                const float rowHeight = ui::LineHeight() + ui::FramePadding().y * 2.0f;
                const bool clicked = ui::HitArea("##Row", glm::vec2(width - ui::ScrollbarSize(), rowHeight));
                const ui::Rect row = ui::item::Bounds();

                if (clicked)
                    Choose(entry.Id);

                if (ui::item::DoubleClicked())
                    Confirm();

                if (!entry.Path.empty())
                    ui::Tooltip(entry.Path, ui::TooltipDelay::None);

                if (selected)
                    painter.FillRect(row, ui::theme::Selection, ui::FrameRounding());
                else if (ui::item::Hovered())
                    painter.FillRect(row, ui::theme::Hover, ui::FrameRounding());

                float x = row.Min.x + ui::FramePadding().x;
                const float y = row.Min.y + ui::FramePadding().y;

                if (icon)
                {
                    painter.Text(glm::vec2(x, y), AssetColor(m_type), icon);
                    x += ui::TextSize(icon).x + ui::InnerSpacing().x;
                }

                painter.Text(glm::vec2(x, y), nameColor, entry.Name);

                if (entry.Engine)
                {
                    x += ui::TextSize(entry.Name).x + ui::InnerSpacing().x;

                    const float size = ui::theme::Scaled(ui::theme::FontSmall);
                    painter.Text(glm::vec2(x, row.Min.y + (rowHeight - size) * 0.5f), ui::theme::TextMuted, "Engine", ui::Font::Regular, size);
                }
            }

            if (selected && m_scrollToSelected)
                ui::ScrollHere(0.5f);
        }

        m_scrollToSelected = false;
    }
}
