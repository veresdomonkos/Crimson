#pragma once
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

#include <glm/vec2.hpp>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    // The items drawn while it lives share a line. Gaps and right-aligned items are asked for before the item they are for.
    class Row
    {
    public:
        // The first item is on the line of the item drawn before the row, if afterLastItem is set
        explicit Row(bool afterLastItem = false);
        ~Row();

        Row(const Row&) = delete;
        Row& operator=(const Row&) = delete;

        // The space before the next item, in place of the usual one
        void Gap(float width);

        // The next item and the ones after it end at the right edge, so this is their width together
        void PushRight(float width);

        // The next item is moved down by this much, such as to center it by the height of an item beside it
        void Offset(float down);
    private:
        friend void PlaceItem();

        bool m_afterLast;
        int m_placed = 0;
        float m_gap = -1.0f;
        float m_rightWidth = -1.0f;
        float m_offset = 0.0f;
        const void* m_window;
    };

    // Equal cells in lines, a new line when the next cell doesn't fit. The cells are drawn with Tile.
    class TileGrid
    {
    public:
        explicit TileGrid(float cellWidth);
        ~TileGrid();

        TileGrid(const TileGrid&) = delete;
        TileGrid& operator=(const TileGrid&) = delete;
    };

    void Spacing();
    void Separator();

    // A scrolling area of its own in the window
    class Region
    {
    public:
        // A size of 0 takes what is left
        explicit Region(std::string_view id, glm::vec2 size = {}, bool bordered = false);
        ~Region();

        Region(const Region&) = delete;
        Region& operator=(const Region&) = delete;

        explicit operator bool() const { return m_visible; }
    private:
        bool m_visible;
    };

    struct Column
    {
        std::string_view Name;
        float Width = 0.0f; // fixed, if the column isn't stretched
        bool Stretch = false;
    };

    struct TableOptions
    {
        bool Resizable = false;
        bool InnerBorders = false;
        bool OuterBorders = false;
        bool RowBackgrounds = false;
        bool ScrollY = false;
        bool FixedFit = false; // columns are as wide as what is in them
        float Height = 0.0f;
    };

    class Table
    {
    public:
        Table(std::string_view id, std::span<const Column> columns, TableOptions options = {});
        ~Table();

        Table(const Table&) = delete;
        Table& operator=(const Table&) = delete;

        explicit operator bool() const { return m_visible; }

        void NextRow();
        void NextColumn();
    private:
        bool m_visible;
    };

    // Calls draw for each row of count that is on screen, so a long list costs what is shown
    void ForVisibleRows(int count, const std::function<void(int)>& draw);

    // What is left of the window, from the cursor
    [[nodiscard]] glm::vec2 Available();

    // The cursor in screen space, where the next item goes
    [[nodiscard]] glm::vec2 Cursor();
    void SetCursor(glm::vec2 position);

    // The width of the next item. Fills takes what is left, less the reserve.
    void NextItemWidth(float width);
    void NextItemFills(float reserve = 0.0f);

    // The width the next item has: what was asked for, or the usual one
    [[nodiscard]] float ItemWidth();

    // Scrolls to the last item, at the ratio of the window it ends up at: 0 is the top, 1 the bottom
    void ScrollHere(float ratio);
    [[nodiscard]] bool IsScrolledToBottom();

    [[nodiscard]] float FontSize();
    [[nodiscard]] float LineHeight();
    [[nodiscard]] float LineHeightWithSpacing();
    [[nodiscard]] float FrameHeight();
    [[nodiscard]] float FrameHeightWithSpacing();
    [[nodiscard]] glm::vec2 TextSize(std::string_view text);

    // What is left of the line past the last item and the usual space, for an item that joins it
    [[nodiscard]] float AvailableAfterItem();

    // What a button with the label is wide, which is its text and its padding
    [[nodiscard]] float ButtonWidth(std::string_view label);
    [[nodiscard]] glm::vec2 ItemSpacing();
    [[nodiscard]] glm::vec2 InnerSpacing();
    [[nodiscard]] glm::vec2 FramePadding();
    [[nodiscard]] float FrameRounding();
    [[nodiscard]] glm::vec2 WindowPadding();
    [[nodiscard]] float ScrollbarSize();

    // Items drawn while it lives are told apart by it, which keeps those with equal labels apart
    class IdScope
    {
    public:
        explicit IdScope(std::string_view id);
        explicit IdScope(int id);
        explicit IdScope(std::size_t hash);
        ~IdScope();

        IdScope(const IdScope&) = delete;
        IdScope& operator=(const IdScope&) = delete;
    };

    // Items drawn while it lives are grayed out and can't be used
    class Disabled
    {
    public:
        explicit Disabled(bool disabled);
        ~Disabled();

        Disabled(const Disabled&) = delete;
        Disabled& operator=(const Disabled&) = delete;
    };

    // A font and size for one scope. The size is a type token, before the monitor's scale, and 0 keeps the current one.
    class FontScope
    {
    public:
        FontScope(Font font, float size);
        explicit FontScope(float size); // the current font
        ~FontScope();

        FontScope(const FontScope&) = delete;
        FontScope& operator=(const FontScope&) = delete;
    };

    // Bold text for one scope, such as a section title
    class BoldScope
    {
    public:
        BoldScope() : m_font(Font::Bold, 0.0f) {}
    private:
        FontScope m_font;
    };

    // A collapsible section of the Inspector, open at first, with a bold title and an optional icon before it. Its id is
    // the label, so it stays open or closed when its icon changes, and sections of the same name share that state.
    class Section
    {
    public:
        explicit Section(std::string_view label, const char* icon = nullptr);
        ~Section();

        Section(const Section&) = delete;
        Section& operator=(const Section&) = delete;

        // Whether it is open, and its content is to be drawn
        explicit operator bool() const { return m_open; }
    private:
        bool m_open;
    };

    // A bold title on a separator line, above a group of fields
    void SectionTitle(std::string_view text);

    // What the editor remembers for a window, by an id in it, and doesn't save. Reads give the fallback for what was
    // never written.
    namespace stored
    {
        [[nodiscard]] bool GetBool(std::string_view id, bool fallback = false);
        void SetBool(std::string_view id, bool value);
        [[nodiscard]] float GetFloat(std::string_view id, float fallback = 0.0f);
        void SetFloat(std::string_view id, float value);
    }
}
