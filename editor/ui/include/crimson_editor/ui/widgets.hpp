#pragma once
#include <memory>
#include <string>
#include <string_view>

#include <glm/vec2.hpp>

#include <crimson/renderer/resource_handles.hpp>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    enum class TextStyle
    {
        Normal,
        Muted
    };

    void Text(std::string_view text, TextStyle style = TextStyle::Normal);
    void Text(std::string_view text, Color color);

    // Wraps at the width from where it starts, or at the edge of the window for 0
    void WrappedText(std::string_view text, Color color, float wrapWidth = 0.0f);
    void Bullet(std::string_view text);

    // A muted line of text for when a panel has nothing to show yet, centered in what is left of it
    void EmptyHint(std::string_view text);

    enum class ButtonSize
    {
        Fit,
        Fill
    };

    [[nodiscard]] bool Button(std::string_view label, ButtonSize size = ButtonSize::Fit);

    // The main action of a dialog or a form. The only button with the accent.
    [[nodiscard]] bool PrimaryButton(std::string_view label);
    [[nodiscard]] bool SmallButton(std::string_view label);

    // A button that stays pressed while its setting is on
    [[nodiscard]] bool ToggleButton(std::string_view label, bool on, glm::vec2 size = {});

    // A toggle that is tinted with a status color while it is on
    [[nodiscard]] bool TintedToggle(std::string_view label, bool on, Color tint);

    // A text-sized rounded box, filled with the selection color and ticked when on. A label after "##" isn't shown.
    bool Checkbox(std::string_view label, bool& value);

    struct InputOptions
    {
        std::string_view Hint;
        bool AutoSelectAll = false;

        // Only Enter counts as a change, and the field returns true then
        bool EnterConfirms = false;

        // Takes the keyboard this frame
        bool Focus = false;
    };

    // A text field for a string of any length. Returns true when the text changed.
    bool InputText(std::string_view id, std::string& text, InputOptions options = {});

    // What a search box matches against. Empty matches everything.
    class SearchFilter
    {
    public:
        SearchFilter();
        ~SearchFilter();

        SearchFilter(const SearchFilter&) = delete;
        SearchFilter& operator=(const SearchFilter&) = delete;

        [[nodiscard]] bool Matches(std::string_view text) const;
        [[nodiscard]] bool IsActive() const;
        void Clear();
    private:
        friend bool SearchBox(std::string_view, SearchFilter&, std::string_view);

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // A text field for a filter, which Escape clears. Returns true when what is searched for changed.
    bool SearchBox(std::string_view id, SearchFilter& filter, std::string_view hint);

    // A drop-down list, which has its items to draw while it is open. The label is shown after it, unless it is after "##".
    class Combo
    {
    public:
        Combo(std::string_view label, std::string_view preview);
        ~Combo();

        Combo(const Combo&) = delete;
        Combo& operator=(const Combo&) = delete;

        explicit operator bool() const { return m_open; }
    private:
        bool m_open;
    };

    struct SelectableOptions
    {
        float Height = 0.0f;

        // Also true on the double click, which is told by the item's DoubleClicked
        bool DoubleClick = false;

        // The highlight covers the row of a table
        bool SpanColumns = false;

        // Items drawn over it can be used
        bool AllowOverlap = false;
    };

    // A row that can be selected. Returns true when it was clicked.
    bool Selectable(std::string_view label, bool selected, SelectableOptions options = {});

    struct TreeRowOptions
    {
        const char* Icon = nullptr;
        Color IconColor;
        std::string_view Label;
        bool Selected = false;
        bool Leaf = false;
        bool DefaultOpen = false;
        bool ForceOpen = false;

        // The label is drawn in the muted color
        bool Muted = false;
    };

    // A row of a tree, with an icon that has a color of its own. Its rows below it are drawn while it is open.
    class TreeRow
    {
    public:
        TreeRow(std::string_view id, const TreeRowOptions& options);
        ~TreeRow();

        TreeRow(const TreeRow&) = delete;
        TreeRow& operator=(const TreeRow&) = delete;

        [[nodiscard]] bool IsOpen() const { return m_open; }

        // The row was clicked, other than on its arrow
        [[nodiscard]] bool Clicked() const { return m_clicked; }
    private:
        bool m_open;
        bool m_clicked;
    };

    struct TileLayout
    {
        Rect Icon;
        glm::vec2 LabelPosition{ 0.0f };
        float LabelWidth = 0.0f;
        bool Clicked = false;
    };

    // A tile of a grid: the frame is drawn here, and where its picture and its name go is told. The frame is drawn rather
    // than a Selectable, whose highlight reaches past the tile into the spacing around it.
    [[nodiscard]] TileLayout Tile(std::string_view id, float width, bool selected);

    // An invisible item of a size, for what is drawn over it. Returns true when it was clicked.
    bool HitArea(std::string_view id, glm::vec2 size, bool allowOverlap = false);

    void Image(TextureHandle texture, glm::vec2 size, bool flipY = false);

    enum class TooltipDelay
    {
        Normal,
        None
    };

    // For the last item
    void Tooltip(std::string_view text, TooltipDelay delay = TooltipDelay::Normal);

    // A click in the window on nothing in it
    [[nodiscard]] bool BackgroundClicked();
}
