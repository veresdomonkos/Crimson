#include "crimson_editor/ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "imgui_convert.hpp"
#include "placement.hpp"

namespace crimson::editor::ui
{
    // What a string view is to ImGui's functions that take a format
    #define VIEW(text) static_cast<int>((text).size()), (text).data()

    void Text(std::string_view text, TextStyle style)
    {
        PlaceItem();

        if (style == TextStyle::Muted)
            ImGui::TextDisabled("%.*s", VIEW(text));
        else
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    void Text(std::string_view text, Color color)
    {
        PlaceItem();
        ImGui::TextColored(ToImVec4(color), "%.*s", VIEW(text));
    }

    void WrappedText(std::string_view text, Color color, float wrapWidth)
    {
        PlaceItem();

        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(color));
        ImGui::PushTextWrapPos(wrapWidth > 0.0f ? ImGui::GetCursorPosX() + wrapWidth : 0.0f);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    void Bullet(std::string_view text)
    {
        PlaceItem();
        ImGui::BulletText("%.*s", VIEW(text));
    }

    void EmptyHint(std::string_view text)
    {
        const std::string line(text);
        const FontScope small(theme::FontSmall);

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float width = std::min(ImGui::CalcTextSize(line.c_str()).x, available.x);
        const ImVec2 cursor = ImGui::GetCursorPos();

        ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0f, (available.x - width) * 0.5f), cursor.y + ImGui::GetStyle().ItemSpacing.y * 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(theme::TextMuted));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    bool Button(std::string_view label, ButtonSize size)
    {
        PlaceItem();
        return ImGui::Button(std::string(label).c_str(), ImVec2(size == ButtonSize::Fill ? -1.0f : 0.0f, 0.0f));
    }

    bool PrimaryButton(std::string_view label)
    {
        PlaceItem();

        ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(theme::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(theme::AccentHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(theme::AccentPressed));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(theme::TextOnAccent));
        const bool clicked = ImGui::Button(std::string(label).c_str());
        ImGui::PopStyleColor(4);

        return clicked;
    }

    bool SmallButton(std::string_view label)
    {
        PlaceItem();
        return ImGui::SmallButton(std::string(label).c_str());
    }

    bool ToggleButton(std::string_view label, bool on, glm::vec2 size)
    {
        PlaceItem();

        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(theme::Selection));
        const bool clicked = ImGui::Button(std::string(label).c_str(), ToImVec2(size));
        if (on)
            ImGui::PopStyleColor();

        return clicked;
    }

    bool TintedToggle(std::string_view label, bool on, Color tint)
    {
        PlaceItem();

        // A hidden level keeps the normal button, so only the shown ones carry a tint
        if (on)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(WithAlpha(tint, theme::TintAlpha)));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(WithAlpha(tint, theme::TintHoverAlpha)));
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(on ? theme::Text : theme::TextMuted));
        const bool clicked = ImGui::Button(std::string(label).c_str());
        ImGui::PopStyleColor(on ? 3 : 1);

        return clicked;
    }

    bool Checkbox(std::string_view text, bool& value)
    {
        PlaceItem();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const std::string labelText(text);
        const char* label = labelText.c_str();

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiID id = window->GetID(label);
        const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);

        // The box is as tall as the text, centered in a row as tall as the other fields
        const float rowHeight = ImGui::GetFrameHeight();
        const float box = std::round(ImGui::GetFontSize());
        const float width = box + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f);

        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + width, pos.y + rowHeight));

        ImGui::ItemSize(bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        if (pressed)
        {
            value = !value;
            ImGui::MarkItemEdited(id);
        }

        const ImVec2 boxMin(pos.x, std::round(pos.y + (rowHeight - box) * 0.5f));
        const ImVec2 boxMax(boxMin.x + box, boxMin.y + box);
        const float rounding = theme::Scaled(3.0f);

        ImDrawList* drawList = window->DrawList;
        ImGui::RenderNavCursor(ImRect(boxMin, boxMax), id);

        if (value)
        {
            // The selection color, as a toggle button that is on has it, with an outline only while hovered
            drawList->AddRectFilled(boxMin, boxMax, ToImU32(theme::Selection), rounding);
            if (hovered || held)
                drawList->AddRect(boxMin, boxMax, ToImU32(theme::TextMuted), rounding, ImDrawFlags_None, theme::Scaled(theme::BorderSize));

            // A tick of two strokes, thin enough to read as a mark rather than a glyph
            const auto at = [&](float x, float y) { return ImVec2(boxMin.x + box * x, boxMin.y + box * y); };
            const std::array tick = { at(0.26f, 0.53f), at(0.43f, 0.69f), at(0.75f, 0.34f) };
            drawList->AddPolyline(tick.data(), static_cast<int>(tick.size()), ToImU32(theme::Text), ImDrawFlags_None,
                std::max(1.5f, box * 0.13f));
        }
        else
        {
            const Color fill = held ? theme::Pressed : hovered ? theme::Hover : theme::FrameBackground;
            drawList->AddRectFilled(boxMin, boxMax, ToImU32(fill), rounding);
            drawList->AddRect(boxMin, boxMax, ToImU32(hovered ? theme::TextMuted : theme::Hover), rounding, ImDrawFlags_None, theme::Scaled(theme::BorderSize));
        }

        if (labelSize.x > 0.0f)
            ImGui::RenderText(ImVec2(boxMax.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);

        return pressed;
    }

    // Lets the field grow with its text, as ImGui asks the callback for room
    static int ResizeText(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            auto* text = static_cast<std::string*>(data->UserData);
            text->resize(static_cast<size_t>(data->BufTextLen));
            data->Buf = text->data();
        }

        return 0;
    }

    bool InputText(std::string_view id, std::string& text, InputOptions options)
    {
        PlaceItem();

        if (options.Focus)
            ImGui::SetKeyboardFocusHere();

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
        if (options.AutoSelectAll)
            flags |= ImGuiInputTextFlags_AutoSelectAll;
        if (options.EnterConfirms)
            flags |= ImGuiInputTextFlags_EnterReturnsTrue;

        const std::string label(id);

        if (options.Hint.empty())
            return ImGui::InputText(label.c_str(), text.data(), text.capacity() + 1, flags, ResizeText, &text);

        return ImGui::InputTextWithHint(label.c_str(), std::string(options.Hint).c_str(), text.data(), text.capacity() + 1, flags, ResizeText, &text);
    }

    struct SearchFilter::Impl
    {
        ImGuiTextFilter Filter;
    };

    SearchFilter::SearchFilter() : m_impl(std::make_unique<Impl>()) {}
    SearchFilter::~SearchFilter() = default;

    bool SearchFilter::Matches(std::string_view text) const
    {
        return m_impl->Filter.PassFilter(std::string(text).c_str());
    }

    bool SearchFilter::IsActive() const
    {
        return m_impl->Filter.IsActive();
    }

    void SearchFilter::Clear()
    {
        m_impl->Filter.Clear();
    }

    bool SearchBox(std::string_view id, SearchFilter& filter, std::string_view hint)
    {
        PlaceItem();

        ImGuiTextFilter& search = filter.m_impl->Filter;
        const bool changed = ImGui::InputTextWithHint(std::string(id).c_str(), std::string(hint).c_str(), search.InputBuf, IM_ARRAYSIZE(search.InputBuf));
        if (changed)
            search.Build();

        // Escape leaves the box, and clears it too, as it does in most search boxes
        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            search.Clear();
            return true;
        }

        return changed;
    }

    Combo::Combo(std::string_view label, std::string_view preview)
    {
        PlaceItem();
        m_open = ImGui::BeginCombo(std::string(label).c_str(), std::string(preview).c_str());
    }

    Combo::~Combo()
    {
        if (m_open)
            ImGui::EndCombo();
    }

    bool Selectable(std::string_view label, bool selected, SelectableOptions options)
    {
        PlaceItem();

        ImGuiSelectableFlags flags = ImGuiSelectableFlags_None;
        if (options.DoubleClick) flags |= ImGuiSelectableFlags_AllowDoubleClick;
        if (options.SpanColumns) flags |= ImGuiSelectableFlags_SpanAllColumns;
        if (options.AllowOverlap) flags |= ImGuiSelectableFlags_AllowOverlap;

        return ImGui::Selectable(std::string(label).c_str(), selected, flags, ImVec2(0.0f, options.Height));
    }

    // A tree node whose icon has a color of its own, which one label can't have. The node is drawn without a label, and the
    // icon and the name are drawn over it, where the label would be. It needs SpanAvailWidth or Framed, so the item
    // covers the row.
    static bool IconTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* icon, Color iconColor, const std::string& label)
    {
        const bool open = ImGui::TreeNodeEx(id, flags, "%s", "");

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        // Where the label would start: after the arrow, and after the frame's padding in a framed node
        float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
        if (flags & ImGuiTreeNodeFlags_Framed)
            x += style.FramePadding.x;
        const float y = std::round((min.y + max.y - ImGui::GetTextLineHeight()) * 0.5f);

        // The node clipped its label to the row, so the drawn one is too
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(min, max, true);

        if (icon && *icon)
        {
            drawList->AddText(ImVec2(x, y), ToImU32(iconColor), icon);
            x += ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x * 2.0f;
        }

        if (!label.empty())
            drawList->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

        drawList->PopClipRect();
        return open;
    }

    TreeRow::TreeRow(std::string_view id, const TreeRowOptions& options)
    {
        PlaceItem();

        // Only the arrow opens and closes it, so the row itself can be clicked and double-clicked
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (options.DefaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (options.Leaf) flags |= ImGuiTreeNodeFlags_Leaf;
        if (options.Selected) flags |= ImGuiTreeNodeFlags_Selected;

        if (options.ForceOpen)
            ImGui::SetNextItemOpen(true);

        if (options.Muted)
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(theme::TextMuted));

        m_open = IconTreeNode(std::string(id).c_str(), flags, options.Icon, options.IconColor, std::string(options.Label));

        if (options.Muted)
            ImGui::PopStyleColor();

        m_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
    }

    TreeRow::~TreeRow()
    {
        if (m_open)
            ImGui::TreePop();
    }

    Section::Section(std::string_view label, const char* icon)
    {
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Framed;

        PlaceItem();

        const std::string text(label);

        // Header is the selection color, and a framed node would take it as its background
        ImGui::PushStyleColor(ImGuiCol_Header, ToImVec4(theme::FrameBackground));
        const BoldScope bold;
        m_open = IconTreeNode(text.c_str(), flags, icon, theme::TextMuted, text);
        ImGui::PopStyleColor();
    }

    Section::~Section()
    {
        if (m_open)
            ImGui::TreePop();
    }

    TileLayout Tile(std::string_view id, float width, bool selected)
    {
        PlaceItem();

        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad = std::round(theme::Scaled(theme::SpaceS));
        const float iconHeight = std::round(width * 0.62f);
        const float height = pad + iconHeight + style.ItemInnerSpacing.y + ImGui::GetTextLineHeight() + pad;

        TileLayout layout;
        layout.Clicked = ImGui::InvisibleButton(std::string(id).c_str(), ImVec2(width, height));

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        const ImGuiCol background = selected ? ImGuiCol_Header : ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_COUNT;
        if (background != ImGuiCol_COUNT)
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(background), theme::Scaled(theme::Radius));

        layout.Icon = Rect{ glm::vec2(min.x + pad, min.y + pad), glm::vec2(max.x - pad, min.y + pad + iconHeight) };
        layout.LabelPosition = glm::vec2(min.x + pad * 0.5f, layout.Icon.Max.y + style.ItemInnerSpacing.y);
        layout.LabelWidth = width - pad;

        ContinueTileLine();
        return layout;
    }

    bool HitArea(std::string_view id, glm::vec2 size, bool allowOverlap)
    {
        PlaceItem();

        if (allowOverlap)
            ImGui::SetNextItemAllowOverlap();

        return ImGui::InvisibleButton(std::string(id).c_str(), ToImVec2(size));
    }

    void Image(TextureHandle texture, glm::vec2 size, bool flipY)
    {
        PlaceItem();

        if (flipY)
            ImGui::Image(ToImTextureID(texture), ToImVec2(size), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        else
            ImGui::Image(ToImTextureID(texture), ToImVec2(size));
    }

    void Tooltip(std::string_view text, TooltipDelay delay)
    {
        const ImGuiHoveredFlags flags = delay == TooltipDelay::Normal ? ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled : ImGuiHoveredFlags_None;

        if (ImGui::IsItemHovered(flags))
            ImGui::SetTooltip("%.*s", VIEW(text));
    }

    bool BackgroundClicked()
    {
        return ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    }
}
