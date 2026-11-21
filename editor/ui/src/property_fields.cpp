#include "crimson_editor/ui/property_fields.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/linear_color.hpp"
#include "crimson_editor/ui/widgets.hpp"
#include "imgui_convert.hpp"

namespace crimson::editor::ui::props
{
    constexpr const char* kResetPopup = "##Reset";

    // Rows don't nest, so the one being drawn can keep its state here
    static bool s_resetClicked = false;

    static bool RightClickedLastItem()
    {
        return ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    }

    // Picking the reset only does something if the value isn't at its default already
    template <typename T>
    static bool ApplyReset(bool picked, T& value, const T* reset)
    {
        if (!picked || !reset || value == *reset)
            return false;

        value = *reset;
        return true;
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

    // A thin track with the part up to the value filled and a round handle, without the value as text, as the field next
    // to it shows that. It moves as ImGui's slider does: dragging, clicking to jump, the arrow keys and logarithmic ranges.
    static bool SliderTrack(float& value, float min, float max, SliderScale scale)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *ImGui::GetCurrentContext();
        const ImGuiID id = window->GetID("##Value");
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + ImGui::CalcItemWidth(), pos.y + ImGui::GetFrameHeight()));

        ImGui::ItemSize(bb, g.Style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        // The whole row height can be grabbed, not only the thin track
        const bool hovered = ImGui::ItemHoverable(bb, id, g.LastItemData.ItemFlags);
        const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None, id);

        if (clicked || g.NavActivateId == id)
        {
            if (clicked)
                ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);

            ImGui::SetActiveID(id, window);
            ImGui::SetFocusID(id, window);
            ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }

        // The handle is the slider's grab, so its center reaches the track's ends exactly at min and max
        const float handleRadius = std::max(3.0f, std::round(ImGui::GetFontSize() * 0.3f));

        ImRect grab;
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, handleRadius * 2.0f);
        const bool changed = ImGui::SliderBehavior(bb, id, ImGuiDataType_Float, &value, &min, &max, "%.3f", (scale == SliderScale::Logarithmic ? ImGuiSliderFlags_Logarithmic : 0) | ImGuiSliderFlags_NoInput, &grab);
        ImGui::PopStyleVar();

        if (changed)
            ImGui::MarkItemEdited(id);

        // SliderBehavior keeps the grab this far inside the box
        constexpr float kGrabPadding = 2.0f;

        const float centerY = bb.GetCenter().y;
        const float trackHalf = std::max(1.0f, std::round(ImGui::GetFontSize() * 0.12f));
        const float trackLeft = bb.Min.x + kGrabPadding;
        const float handleX = grab.Max.x > grab.Min.x ? grab.GetCenter().x : trackLeft + handleRadius;
        const bool lit = hovered || g.ActiveId == id;

        ImDrawList* drawList = window->DrawList;
        ImGui::RenderNavCursor(bb, id);
        drawList->AddRectFilled(ImVec2(trackLeft, centerY - trackHalf), ImVec2(bb.Max.x - kGrabPadding, centerY + trackHalf),
            ToImU32(theme::FrameBackground), trackHalf);
        drawList->AddRectFilled(ImVec2(trackLeft, centerY - trackHalf), ImVec2(handleX, centerY + trackHalf),
            ToImU32(theme::TextMuted), trackHalf);
        drawList->AddCircleFilled(ImVec2(handleX, centerY), handleRadius,
            ToImU32(lit ? theme::Text : WithAlpha(theme::Text, 0.8f)));

        return changed;
    }

    bool SliderField(float& value, float min, float max, const char* format, SliderScale scale)
    {
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float fieldWidth = ImGui::GetFontSize() * 4.5f;
        const float sliderWidth = std::max(1.0f, ImGui::CalcItemWidth() - fieldWidth - spacing);

        ImGui::SetNextItemWidth(sliderWidth);
        bool changed = SliderTrack(value, min, max, scale);
        ResetTarget();

        ImGui::SameLine(0.0f, spacing);
        ImGui::SetNextItemWidth(fieldWidth);

        // A typed value can be anything, so it is held to the slider's range
        if (ImGui::InputFloat("##Field", &value, 0.0f, 0.0f, format))
        {
            value = std::clamp(value, min, max);
            changed = true;
        }

        ResetTarget();
        return changed;
    }

    bool Slider(const char* label, float& value, float min, float max, const float* reset, SliderScale scale)
    {
        BeginRow(label);
        bool changed = SliderField(value, min, max, "%.3f", scale);

        changed |= ApplyReset(EndRow(reset != nullptr), value, reset);
        return changed;
    }

    bool Floats(const char* label, float* values, int count, float speed, float min, float max, const float* reset)
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
            ImGui::GetWindowDrawList()->AddRectFilled(stripeMin, stripeMax, ToImU32(kColors[i]),
                ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);

            ImGui::Dummy(ImVec2(stripeWidth, ImGui::GetFrameHeight()));
            ImGui::SameLine(0.0f, 0.0f);

            ImGui::SetNextItemWidth(std::max(1.0f, fieldWidth - stripeWidth));
            changed |= ImGui::DragFloat("##Value", &values[i], speed, min, max, "%.3f", min < max ? ImGuiSliderFlags_AlwaysClamp : 0);
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
        return Floats(label, &value.x, 3, speed, 0.0f, 0.0f, reset ? &reset->x : nullptr);
    }

    bool Color(const char* label, float* linearColor, int channels, const float* reset)
    {
        // The swatch has a menu of its own on right-click, so the reset is on the label alone
        BeginRow(label);
        bool changed = linear_color::Edit("##Value", linearColor, channels);

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
        bool changed = ui::Checkbox("##Value", value);
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
