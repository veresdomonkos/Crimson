#pragma once
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <crimson/asset_manager/asset_manager.hpp>

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

    // What the picker lists: None first, then the project's assets, then the engine's, each by name without regard to
    // case. Internal ones are left out, and the search matches the file name, also without regard to case.
    [[nodiscard]] std::vector<PickerEntry> PickerEntries(std::span<const AssetInfo* const> assets, std::string_view search);

    // The index after moving by dx and dy in a grid of that many columns (1 for a list), kept within the entries.
    // Nothing selected (-1) goes to the first. -1 if there are no entries.
    [[nodiscard]] int MovePickerSelection(int index, int count, int dx, int dy, int columns);

    // The window a field opens to pick an asset of one type from. Only one is open at a time: fields open it and take
    // what it chose on the next frame, as assets change between frames.
    class AssetPicker
    {
    public:
        enum class View { List, Grid };

        // Clears the search and any choice. The owner is the field's ImGui id, and below is where its window goes.
        void Open(ImGuiID owner, AssetType type, AssetID current, ImVec2 below);

        // Keeps a choice that wasn't taken yet, so a double-click's choice still reaches its field
        void Close();

        // Escape: closes, and chooses what the field had when the picker opened, if anything was chosen since
        void Cancel();

        // Closes and drops a choice, as the field it was for is gone
        void Reset();

        [[nodiscard]] bool IsOpen() const { return m_open; }
        [[nodiscard]] bool IsOpenFor(ImGuiID owner) const { return m_open && m_owner == owner; }

        // Selects and applies (a click, the arrow keys)
        void Choose(AssetID id);

        // Applies the selection and closes (a double-click, Enter)
        void Confirm();

        void SetView(View view) { m_view = view; }
        void SetSearch(std::string_view search);

        [[nodiscard]] View GetView() const { return m_view; }
        [[nodiscard]] AssetID Selected() const { return m_selected; }

        // What was chosen since the last call, once, and only for the field that opened the picker
        [[nodiscard]] std::optional<AssetID> TakeChoice(ImGuiID owner);

        // Call once a frame, after the panels
        void Draw(const AssetManager& assets, AssetThumbnails& thumbnails);
    private:
        void DrawEntries(const AssetManager& assets, AssetThumbnails& thumbnails, const std::vector<PickerEntry>& entries, int selectedIndex);

        ImGuiID m_owner = 0;
        AssetType m_type = AssetType::None;
        AssetID m_selected;
        AssetID m_original; // what the field had when the picker opened
        std::optional<AssetID> m_pending;
        bool m_changed = false; // something was applied since it opened
        std::array<char, 128> m_search{};
        View m_view = View::List;
        bool m_open = false;
        bool m_justOpened = false;
        bool m_scrollToSelected = false;
        ImVec2 m_position;
    };
}
