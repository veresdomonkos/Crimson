#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor
{
    class AssetThumbnails;
}

namespace crimson::editor::ui
{
    struct PickerEntry
    {
        AssetID Id; // empty for None
        std::string Name;
        std::string Path;
        bool Engine = false;
    };

    namespace asset_picker
    {
        // What the picker lists: None first, then the project's assets, then the engine's, each by name without regard to
        // case. Internal ones are left out, and the search matches the file name, also without regard to case.
        [[nodiscard]] std::vector<PickerEntry> Entries(std::span<const AssetInfo* const> assets, std::string_view search);

        // The index after moving by dx and dy in a grid of that many columns (1 for a list), kept within the entries.
        // Nothing selected (-1) goes to the first. -1 if there are no entries.
        [[nodiscard]] int MoveSelection(int index, int count, int dx, int dy, int columns);
    }

    // The window a field opens to pick an asset of one type from. Only one is open at a time: fields open it and take
    // what it chose on the next frame, as assets change between frames.
    class AssetPicker
    {
    public:
        enum class View { List, Grid };

        // Clears the search and any choice. The owner is the field's id, and below is where its window goes.
        void Open(ui::WidgetId owner, AssetType type, AssetID current, glm::vec2 below);

        // Keeps a choice that wasn't taken yet, so a double-click's choice still reaches its field
        void Close();

        // Escape: closes, and chooses what the field had when the picker opened, if anything was chosen since
        void Cancel();

        [[nodiscard]] bool IsOpen() const { return m_open; }
        [[nodiscard]] bool IsOpenFor(ui::WidgetId owner) const { return m_open && m_owner == owner; }

        // Selects and applies (a click, the arrow keys)
        void Choose(AssetID id);

        // Applies the selection and closes (a double-click, Enter)
        void Confirm();

        void SetView(View view) { m_view = view; }
        void SetSearch(std::string_view search);

        [[nodiscard]] View GetView() const { return m_view; }
        [[nodiscard]] AssetID Selected() const { return m_selected; }

        // What was chosen since the last call, once, and only for the field that opened the picker, and only while that
        // field is drawn. Every field calls it each frame it is drawn, and the picker closes once its field doesn't.
        [[nodiscard]] std::optional<AssetID> TakeChoice(ui::WidgetId owner);

        // Closes the picker and drops a choice if the field that opened it wasn't drawn since the last call. Draw does
        // this first, so it only needs calling by itself without drawing.
        void EndFrame();

        // Call once a frame, after the panels
        void Draw(const AssetManager& assets, AssetThumbnails& thumbnails);
    private:
        // Closes and drops a choice, as the field it was for is gone
        void Drop();

        void DrawEntries(const AssetManager& assets, AssetThumbnails& thumbnails, const std::vector<PickerEntry>& entries, int selectedIndex);

        ui::WidgetId m_owner = 0;
        AssetType m_type;
        AssetID m_selected;
        AssetID m_original; // what the field had when the picker opened
        std::optional<AssetID> m_pending;
        bool m_changed = false; // something was applied since it opened
        std::string m_search;
        View m_view = View::List;
        bool m_open = false;
        bool m_fieldDrawn = false; // the field that opened it was drawn since the last EndFrame
        bool m_justOpened = false;
        bool m_scrollToSelected = false;
        glm::vec2 m_position{ 0.0f };
    };
}
