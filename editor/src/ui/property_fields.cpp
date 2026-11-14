#include "crimson_editor/ui/property_fields.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <string>

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

    bool Int(const char* label, int& value, float speed, int min, int max, const int* reset)
    {
        BeginRow(label);
        bool changed = ImGui::DragInt("##Value", &value, speed, min, max);
        ResetTarget();

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool String(const char* label, std::string& value, const std::string* reset)
    {
        BeginRow(label);

        std::array<char, 256> buffer{};
        value.copy(buffer.data(), buffer.size() - 1);

        bool changed = ImGui::InputText("##Value", buffer.data(), buffer.size());
        if (changed)
            value = buffer.data();

        ResetTarget();

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool SliderField(float& value, float min, float max, const char* format, ImGuiSliderFlags flags)
    {
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float fieldWidth = ImGui::GetFontSize() * 4.5f;
        const float sliderWidth = std::max(1.0f, ImGui::CalcItemWidth() - fieldWidth - spacing);

        ImGui::SetNextItemWidth(sliderWidth);
        bool changed = ImGui::SliderFloat("##Value", &value, min, max, format, flags);
        ResetTarget();

        ImGui::SameLine(0.0f, spacing);
        ImGui::SetNextItemWidth(fieldWidth);

        // A typed value can be anything, so it is held to the slider's range
        if (ImGui::InputFloat("##Field", &value, 0.0f, 0.0f, "%.3f"))
        {
            value = std::clamp(value, min, max);
            changed = true;
        }

        ResetTarget();
        return changed;
    }

    bool Slider(const char* label, float& value, float min, float max, const float* reset, ImGuiSliderFlags flags)
    {
        BeginRow(label);
        bool changed = SliderField(value, min, max, "%.3f", flags);

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool Floats(const char* label, float* values, int count, float speed, const float* reset)
    {
        static constexpr std::array kAxes = { "X", "Y", "Z", "W" };
        static constexpr std::array kColors = { theme::AxisX, theme::AxisY, theme::AxisZ, theme::AxisW };

        count = std::clamp(count, 1, 4);

        BeginRow(label);

        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float fieldWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
        const float stripeWidth = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.16f));

        bool changed = false;

        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, spacing);

            ImGui::PushID(i);

            // Which axis it is, as a thin stripe of the gizmo's color right before the field. It has its own space, so
            // a long number never runs under it, and it is only drawn, so it can't be clicked by accident.
            const ImVec2 stripeMin = ImGui::GetCursorScreenPos();
            const ImVec2 stripeMax(stripeMin.x + stripeWidth, stripeMin.y + ImGui::GetFrameHeight());
            ImGui::GetWindowDrawList()->AddRectFilled(stripeMin, stripeMax, ImGui::GetColorU32(kColors[i]),
                ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);

            ImGui::Dummy(ImVec2(stripeWidth, ImGui::GetFrameHeight()));
            ImGui::SameLine(0.0f, 0.0f);

            ImGui::SetNextItemWidth(std::max(1.0f, fieldWidth - stripeWidth));
            changed |= ImGui::DragFloat("##Value", &values[i], speed, 0.0f, 0.0f, "%.3f");
            ResetTarget();
            ImGui::SetItemTooltip("%s", kAxes[i]);

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
