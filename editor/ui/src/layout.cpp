#include "crimson_editor/ui/layout.hpp"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "imgui_convert.hpp"
#include "placement.hpp"
#include "theme_style.hpp"

namespace crimson::editor::ui
{
    // The rows and grids being drawn, the innermost last
    static std::vector<Row*> s_rows;
    static std::vector<float> s_gridCells;

    WidgetId IdOf(std::string_view id)
    {
        return ImGui::GetID(id.data(), id.data() + id.size());
    }

    Row::Row(bool afterLastItem)
        : m_afterLast(afterLastItem),
          m_window(ImGui::GetCurrentWindow())
    {
        s_rows.push_back(this);
    }

    Row::~Row()
    {
        s_rows.pop_back();
    }

    void Row::Gap(float width)
    {
        m_gap = width;
    }

    void Row::PushRight(float width)
    {
        m_rightWidth = width;
    }

    void Row::Offset(float down)
    {
        m_offset = down;
    }

    void PlaceItem()
    {
        if (s_rows.empty())
            return;

        Row& row = *s_rows.back();
        if (row.m_window != ImGui::GetCurrentWindow())
            return;

        const bool first = row.m_placed++ == 0;
        const float gap = row.m_gap;
        const float right = row.m_rightWidth;
        const float offset = row.m_offset;
        row.m_gap = -1.0f;
        row.m_rightWidth = -1.0f;
        row.m_offset = 0.0f;

        if (right >= 0.0f)
        {
            if (first && !row.m_afterLast)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - right));
            else
                ImGui::SameLine(std::max(0.0f, ImGui::GetContentRegionMax().x - right));
        }
        else if (!first || row.m_afterLast)
        {
            ImGui::SameLine(0.0f, gap);
        }

        if (offset != 0.0f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
    }

    TileGrid::TileGrid(float cellWidth)
    {
        s_gridCells.push_back(cellWidth);
    }

    TileGrid::~TileGrid()
    {
        s_gridCells.pop_back();
    }

    void ContinueTileLine()
    {
        if (s_gridCells.empty())
            return;

        const float right = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;

        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + s_gridCells.back() <= right + 0.5f)
            ImGui::SameLine();
    }

    void Spacing()
    {
        PlaceItem();
        ImGui::Spacing();
    }

    void Separator()
    {
        ImGui::Separator();
    }

    Region::Region(std::string_view id, glm::vec2 size, bool bordered)
    {
        PlaceItem();

        const std::string name(id);
        m_visible = ImGui::BeginChild(name.c_str(), ToImVec2(size), bordered ? ImGuiChildFlags_Borders : ImGuiChildFlags_None);
    }

    Region::~Region()
    {
        ImGui::EndChild();
    }

    Table::Table(std::string_view id, std::span<const Column> columns, TableOptions options)
    {
        ImGuiTableFlags flags = ImGuiTableFlags_NoSavedSettings;
        if (options.Resizable) flags |= ImGuiTableFlags_Resizable;
        if (options.InnerBorders) flags |= ImGuiTableFlags_BordersInnerV;
        if (options.OuterBorders) flags |= ImGuiTableFlags_BordersOuter;
        if (options.RowBackgrounds) flags |= ImGuiTableFlags_RowBg;
        if (options.ScrollY) flags |= ImGuiTableFlags_ScrollY;
        if (options.FixedFit) flags |= ImGuiTableFlags_SizingFixedFit;

        PlaceItem();

        const std::string name(id);
        m_visible = ImGui::BeginTable(name.c_str(), static_cast<int>(columns.size()), flags, ImVec2(0.0f, options.Height));
        if (!m_visible)
            return;

        for (const Column& column : columns)
        {
            const std::string columnName(column.Name);
            ImGui::TableSetupColumn(columnName.c_str(), column.Stretch ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed, column.Width);
        }
    }

    Table::~Table()
    {
        if (m_visible)
            ImGui::EndTable();
    }

    void Table::NextRow()
    {
        ImGui::TableNextRow();
    }

    void Table::NextColumn()
    {
        ImGui::TableNextColumn();
    }

    void ForVisibleRows(int count, const std::function<void(int)>& draw)
    {
        ImGuiListClipper clipper;
        clipper.Begin(count);

        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                draw(row);
        }
    }

    glm::vec2 Available() { return FromImVec2(ImGui::GetContentRegionAvail()); }
    glm::vec2 Cursor() { return FromImVec2(ImGui::GetCursorScreenPos()); }
    void SetCursor(glm::vec2 position) { ImGui::SetCursorScreenPos(ToImVec2(position)); }

    void NextItemWidth(float width) { ImGui::SetNextItemWidth(width); }
    void NextItemFills(float reserve) { ImGui::SetNextItemWidth(-(reserve > 0.0f ? reserve : FLT_MIN)); }
    float ItemWidth() { return ImGui::CalcItemWidth(); }

    void ScrollHere(float ratio) { ImGui::SetScrollHereY(ratio); }
    bool IsScrolledToBottom() { return ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f; }

    float FontSize() { return ImGui::GetFontSize(); }
    float LineHeight() { return ImGui::GetTextLineHeight(); }
    float LineHeightWithSpacing() { return ImGui::GetTextLineHeightWithSpacing(); }
    float FrameHeight() { return ImGui::GetFrameHeight(); }
    float FrameHeightWithSpacing() { return ImGui::GetFrameHeightWithSpacing(); }

    glm::vec2 TextSize(std::string_view text)
    {
        return FromImVec2(ImGui::CalcTextSize(text.data(), text.data() + text.size()));
    }

    float AvailableAfterItem()
    {
        const float right = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
        return right - (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x);
    }

    float ButtonWidth(std::string_view label)
    {
        return ImGui::CalcTextSize(label.data(), label.data() + label.size(), true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    glm::vec2 ItemSpacing() { return FromImVec2(ImGui::GetStyle().ItemSpacing); }
    glm::vec2 InnerSpacing() { return FromImVec2(ImGui::GetStyle().ItemInnerSpacing); }
    glm::vec2 FramePadding() { return FromImVec2(ImGui::GetStyle().FramePadding); }
    float FrameRounding() { return ImGui::GetStyle().FrameRounding; }
    glm::vec2 WindowPadding() { return FromImVec2(ImGui::GetStyle().WindowPadding); }
    float ScrollbarSize() { return ImGui::GetStyle().ScrollbarSize; }

    IdScope::IdScope(std::string_view id)
    {
        ImGui::PushID(id.data(), id.data() + id.size());
    }

    IdScope::IdScope(int id)
    {
        ImGui::PushID(id);
    }

    IdScope::IdScope(std::size_t hash)
    {
        ImGui::PushID(reinterpret_cast<const void*>(hash));
    }

    IdScope::~IdScope()
    {
        ImGui::PopID();
    }

    Disabled::Disabled(bool disabled)
    {
        ImGui::BeginDisabled(disabled);
    }

    Disabled::~Disabled()
    {
        ImGui::EndDisabled();
    }

    FontScope::FontScope(Font font, float size)
    {
        ImGui::PushFont(theme::FontOf(font), size);
    }

    FontScope::FontScope(float size)
    {
        ImGui::PushFont(nullptr, size);
    }

    FontScope::~FontScope()
    {
        ImGui::PopFont();
    }

    void SectionTitle(std::string_view text)
    {
        const std::string label(text);
        const BoldScope bold;

        PlaceItem();
        ImGui::SeparatorText(label.c_str());
    }

    namespace stored
    {
        bool GetBool(std::string_view id, bool fallback)
        {
            return ImGui::GetStateStorage()->GetBool(IdOf(id), fallback);
        }

        void SetBool(std::string_view id, bool value)
        {
            ImGui::GetStateStorage()->SetBool(IdOf(id), value);
        }

        float GetFloat(std::string_view id, float fallback)
        {
            return ImGui::GetStateStorage()->GetFloat(IdOf(id), fallback);
        }

        void SetFloat(std::string_view id, float value)
        {
            ImGui::GetStateStorage()->SetFloat(IdOf(id), value);
        }
    }
}
