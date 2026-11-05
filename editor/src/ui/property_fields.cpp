#include "crimson_editor/ui/property_fields.hpp"

#include <algorithm>
#include <array>
#include <cfloat>

#include <imgui.h>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/linear_color.hpp"

namespace crimson::editor::ui::props
{
    namespace
    {
        constexpr const char* kResetPopup = "##Reset";

        // Rows don't nest, so the one being drawn can keep its state here
        bool s_resetClicked = false;

        bool RightClickedLastItem()
        {
            return ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        }

        ImVec4 Lighter(const ImVec4& color, float amount)
        {
            return ImVec4(std::min(color.x + amount, 1.0f), std::min(color.y + amount, 1.0f), std::min(color.z + amount, 1.0f), color.w);
        }

        // Picking the reset only does something if the value isn't at its default already
        template <typename T>
        bool ApplyReset(bool picked, T& value, const T* reset)
        {
            if (!picked || !reset || value == *reset)
                return false;

            value = *reset;
            return true;
        }
    }

    bool BeginTable(const char* id)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;

        if (!ImGui::BeginTable(id, 2, flags))
            return false;

        // Wide enough for most labels, without squeezing the values in a narrow panel
        const float fontSize = ImGui::GetFontSize();
        const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.35f, fontSize * 4.0f, fontSize * 8.0f);

        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void EndTable()
    {
        ImGui::EndTable();
    }

    void BeginRow(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        // The column cuts a long label off, so the whole of it is a hover away
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label).x > ImGui::GetColumnWidth())
            ImGui::SetTooltip("%s", label);

        s_resetClicked = RightClickedLastItem();

        ImGui::TableNextColumn();
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void ResetTarget()
    {
        s_resetClicked = s_resetClicked || RightClickedLastItem();
    }

    bool EndRow(bool resettable)
    {
        bool picked = false;

        if (resettable)
        {
            if (s_resetClicked)
                ImGui::OpenPopup(kResetPopup);

            if (ImGui::BeginPopup(kResetPopup))
            {
                picked = ImGui::MenuItem("Reset to default");
                ImGui::EndPopup();
            }
        }

        s_resetClicked = false;
        ImGui::PopID();
        return picked;
    }

    bool Float(const char* label, float& value, float speed, float min, float max, const float* reset)
    {
        BeginRow(label);
        bool changed = ImGui::DragFloat("##Value", &value, speed, min, max);
        ResetTarget();

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool Slider(const char* label, float& value, float min, float max, const float* reset)
    {
        BeginRow(label);
        bool changed = ImGui::SliderFloat("##Value", &value, min, max);
        ResetTarget();

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool Floats(const char* label, float* values, int count, float speed, const float* reset)
    {
        static constexpr std::array kTags = { "X", "Y", "Z", "W" };
        static constexpr std::array kColors = { theme::AxisX, theme::AxisY, theme::AxisZ, theme::AxisW };

        count = std::clamp(count, 1, 4);

        BeginRow(label);

        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float tagWidth = ImGui::GetFrameHeight();
        const float fieldWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) / static_cast<float>(count) - tagWidth);

        bool changed = false;

        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, spacing);

            ImGui::PushID(i);

            // The tag is a button: clicking it puts that axis back
            ImGui::PushStyleColor(ImGuiCol_Button, kColors[i]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighter(kColors[i], 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Lighter(kColors[i], 0.2f));

            if (ImGui::Button(kTags[i], ImVec2(tagWidth, 0.0f)) && reset && values[i] != reset[i])
            {
                values[i] = reset[i];
                changed = true;
            }

            ImGui::PopStyleColor(3);

            if (reset)
                ImGui::SetItemTooltip("Click to reset %s", kTags[i]);

            ResetTarget();

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetNextItemWidth(fieldWidth);
            changed |= ImGui::DragFloat("##Value", &values[i], speed, 0.0f, 0.0f, "%.3f");
            ResetTarget();

            ImGui::PopID();
        }

        if (EndRow(reset != nullptr))
        {
            for (int i = 0; i < count; ++i)
            {
                changed = changed || values[i] != reset[i];
                values[i] = reset[i];
            }
        }

        return changed;
    }

    bool Vec3(const char* label, glm::vec3& value, float speed, const glm::vec3* reset)
    {
        return Floats(label, &value.x, 3, speed, reset ? &reset->x : nullptr);
    }

    bool Color(const char* label, float* linearColor, int channels, const float* reset)
    {
        // The swatch has a menu of its own on right-click, so the reset is on the label alone
        BeginRow(label);
        bool changed = LinearColorEdit("##Value", linearColor, channels);

        if (EndRow(reset != nullptr))
        {
            for (int i = 0; i < channels; ++i)
            {
                changed = changed || linearColor[i] != reset[i];
                linearColor[i] = reset[i];
            }
        }

        return changed;
    }

    bool Checkbox(const char* label, bool& value, const bool* reset)
    {
        BeginRow(label);
        bool changed = ImGui::Checkbox("##Value", &value);
        ResetTarget();

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool Combo(const char* label, int& value, const char* const* items, int count, const int* reset)
    {
        BeginRow(label);
        bool changed = ImGui::Combo("##Value", &value, items, count);
        ResetTarget();

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    void Text(const char* label, const char* value)
    {
        BeginRow(label);
        ImGui::TextUnformatted(value);
        EndRow(false);
    }
}
