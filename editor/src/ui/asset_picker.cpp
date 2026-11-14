#include "crimson_editor/ui/asset_picker.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "crimson_editor/asset_thumbnails.hpp"
#include "crimson_editor/ui/asset_tiles.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        constexpr const char* kPopupId = "##AssetPicker";
        constexpr int kGridColumns = 4;

        // The window is sized in ems, so the popup keeps its size while the entries change
        constexpr float kBodyWidthEms = 22.0f;
        constexpr float kBodyHeightEms = 24.0f;

        std::string Lowered(std::string_view text)
        {
            std::string lowered(text);
            std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lowered;
        }

        int IndexOf(const std::vector<PickerEntry>& entries, AssetID id)
        {
            const auto it = std::ranges::find(entries, id, &PickerEntry::Id);
            return it == entries.end() ? -1 : static_cast<int>(it - entries.begin());
        }
    }

    std::vector<PickerEntry> PickerEntries(std::span<const AssetInfo* const> assets, std::string_view search)
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

    int MovePickerSelection(int index, int count, int dx, int dy, int columns)
    {
        if (count <= 0)
            return -1;

        if (index < 0)
            return 0;

        return std::clamp(index + dx + dy * columns, 0, count - 1);
    }

    void AssetPicker::Open(ImGuiID owner, AssetType type, AssetID current, ImVec2 below)
    {
        m_owner = owner;
        m_type = type;
        m_selected = current;
        m_original = current;
        m_pending.reset();
        m_changed = false;
        m_search = {};
        m_open = true;
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

    void AssetPicker::Reset()
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
        m_search = {};
        search.copy(m_search.data(), m_search.size() - 1);
    }

    std::optional<AssetID> AssetPicker::TakeChoice(ImGuiID owner)
    {
        if (!m_pending || m_owner != owner)
            return std::nullopt;

        const std::optional<AssetID> choice = m_pending;
        m_pending.reset();
        return choice;
    }

    void AssetPicker::Draw(const AssetManager& assets, AssetThumbnails& thumbnails)
    {
        // Before the popup, as ImGui closes it on Escape by itself
        if (m_open && !m_justOpened && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            Cancel();

        if (m_justOpened)
        {
            ImGui::OpenPopup(kPopupId);
            m_justOpened = false;
        }

        if (!m_open && !ImGui::IsPopupOpen(kPopupId))
            return;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float bodyWidth = std::round(ImGui::GetFontSize() * kBodyWidthEms);
        const float bodyHeight = std::round(ImGui::GetFontSize() * kBodyHeightEms);

        // Kept on screen by what the window takes: the body, the search row, the title and the padding
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 size(bodyWidth + style.WindowPadding.x * 2.0f, bodyHeight + ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() + style.ItemSpacing.y * 2.0f + style.WindowPadding.y * 2.0f);
        const ImVec2 position(std::clamp(m_position.x, viewport->WorkPos.x, std::max(viewport->WorkPos.x, viewport->WorkPos.x + viewport->WorkSize.x - size.x)),
                              std::clamp(m_position.y, viewport->WorkPos.y, std::max(viewport->WorkPos.y, viewport->WorkPos.y + viewport->WorkSize.y - size.y)));
        ImGui::SetNextWindowPos(position, ImGuiCond_Appearing);

        // Without navigation, as the arrow keys are the picker's own and would move off the search box
        if (!ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoNavInputs))
        {
            m_open = false;
            return;
        }

        if (!m_open)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        {
            const theme::ScopedBold bold;
            ImGui::Text("Select %.*s", static_cast<int>(ToString(m_type).size()), ToString(m_type).data());
        }

        const bool appearing = ImGui::IsWindowAppearing();
        const float buttonWidth = ImGui::CalcTextSize("Grid").x + style.FramePadding.x * 2.0f;

        if (appearing)
            ImGui::SetKeyboardFocusHere();

        const std::string hint = std::string(kIconSearch) + " Search";
        ImGui::SetNextItemWidth(bodyWidth - buttonWidth * 2.0f - style.ItemSpacing.x * 2.0f);
        ImGui::InputTextWithHint("##Search", hint.c_str(), m_search.data(), m_search.size());
        const bool typing = ImGui::IsItemActive() && m_search[0] != '\0';

        ImGui::SameLine();
        if (theme::ToggleButton("List", m_view == View::List, ImVec2(buttonWidth, 0.0f)))
            m_view = View::List;

        ImGui::SameLine();
        if (theme::ToggleButton("Grid", m_view == View::Grid, ImVec2(buttonWidth, 0.0f)))
            m_view = View::Grid;

        const std::vector<const AssetInfo*> listed = assets.List(m_type);
        const std::vector<PickerEntry> entries = PickerEntries(listed, m_search.data());
        int selectedIndex = IndexOf(entries, m_selected);

        const bool grid = m_view == View::Grid;
        const int columns = grid ? kGridColumns : 1;

        // The left and right arrows move the cursor in the search box while there is something to move through
        int dx = 0;
        int dy = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = 1;
        if (grid && !typing && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -1;
        if (grid && !typing && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = 1;

        if (dx != 0 || dy != 0)
        {
            if (const int moved = MovePickerSelection(selectedIndex, static_cast<int>(entries.size()), dx, dy, columns); moved >= 0)
            {
                Choose(entries[moved].Id);
                selectedIndex = moved;
                m_scrollToSelected = true;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            Confirm();

        if (m_open)
            DrawEntries(assets, thumbnails, entries, selectedIndex);

        if (!m_open)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void AssetPicker::DrawEntries(const AssetManager& assets, AssetThumbnails& thumbnails, const std::vector<PickerEntry>& entries, int selectedIndex)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float width = std::round(ImGui::GetFontSize() * kBodyWidthEms);
        const float height = std::round(ImGui::GetFontSize() * kBodyHeightEms);
        const bool grid = m_view == View::Grid;

        const float tileSize = std::floor((width - style.ScrollbarSize - style.ItemSpacing.x * (kGridColumns - 1)) / kGridColumns);

        ImGui::BeginChild("##Entries", ImVec2(width, height));

        for (int index = 0; index < static_cast<int>(entries.size()); ++index)
        {
            const PickerEntry& entry = entries[index];
            const bool selected = index == selectedIndex;
            const ImVec4 nameColor = entry.Engine ? theme::TextMuted : style.Colors[ImGuiCol_Text];
            const char* icon = entry.Id ? AssetIcon(m_type) : nullptr; // None has none

            ImGui::PushID(index);

            if (grid)
            {
                const TileLayout tile = DrawTileFrame("##Tile", tileSize, selected);
                if (tile.Clicked)
                    Choose(entry.Id);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Confirm();

                if (ImGui::IsItemHovered() && !entry.Path.empty())
                    ImGui::SetTooltip("%s", entry.Path.c_str());

                ImDrawList* drawList = ImGui::GetWindowDrawList();

                // Only pictures that are on screen get loaded
                ImTextureID picture = 0;
                if (m_type == AssetType::Texture && entry.Id && ImGui::IsItemVisible())
                    picture = thumbnails.Get(assets.As<AssetType::Texture>(entry.Id));

                if (picture)
                {
                    const float side = std::min(tile.IconMax.x - tile.IconMin.x, tile.IconMax.y - tile.IconMin.y);
                    const ImVec2 center((tile.IconMin.x + tile.IconMax.x) * 0.5f, (tile.IconMin.y + tile.IconMax.y) * 0.5f);
                    drawList->AddImage(picture, ImVec2(center.x - side * 0.5f, center.y - side * 0.5f), ImVec2(center.x + side * 0.5f, center.y + side * 0.5f));
                }
                else if (icon)
                {
                    DrawTileIcon(drawList, tile.IconMin, tile.IconMax, icon, theme::AssetColor(m_type));
                }

                DrawClippedText(drawList, tile.LabelPosition, tile.LabelWidth, entry.Name.c_str(), ImGui::GetColorU32(nameColor));

                if ((index + 1) % kGridColumns != 0 && index + 1 < static_cast<int>(entries.size()))
                    ImGui::SameLine();
            }
            else
            {
                // Drawn rather than a Selectable, whose highlight is square and reaches past the row into the spacing, where
                // the child cuts it off at the top and on the left
                const float rowHeight = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;
                const bool clicked = ImGui::InvisibleButton("##Row", ImVec2(width - style.ScrollbarSize, rowHeight));
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();

                if (clicked)
                    Choose(entry.Id);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Confirm();

                if (ImGui::IsItemHovered() && !entry.Path.empty())
                    ImGui::SetTooltip("%s", entry.Path.c_str());

                ImDrawList* drawList = ImGui::GetWindowDrawList();

                const ImGuiCol background = selected ? ImGuiCol_Header : ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_COUNT;
                if (background != ImGuiCol_COUNT)
                    drawList->AddRectFilled(min, max, ImGui::GetColorU32(background), style.FrameRounding);

                float x = min.x + style.FramePadding.x;
                const float y = min.y + style.FramePadding.y;

                if (icon)
                {
                    drawList->AddText(ImVec2(x, y), ImGui::GetColorU32(theme::AssetColor(m_type)), icon);
                    x += ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x;
                }

                drawList->AddText(ImVec2(x, y), ImGui::GetColorU32(nameColor), entry.Name.c_str());

                if (entry.Engine)
                {
                    x += ImGui::CalcTextSize(entry.Name.c_str()).x + style.ItemInnerSpacing.x;

                    ImFont* font = theme::RegularFont();
                    const float size = theme::Scaled(theme::FontSmall);
                    drawList->AddText(font, size, ImVec2(x, min.y + (rowHeight - size) * 0.5f), ImGui::GetColorU32(theme::TextMuted), "Engine");
                }
            }

            if (selected && m_scrollToSelected)
                ImGui::SetScrollHereY(0.5f);

            ImGui::PopID();
        }

        m_scrollToSelected = false;
        ImGui::EndChild();
    }
}
