#include "crimson_editor/ui/linear_color.hpp"

#include <algorithm>
#include <array>

#include <imgui.h>

namespace crimson::editor::ui
{
    bool LinearColorEdit(const char* label, float* linearColor, int channels)
    {
        std::array<float, 4> shown{};

        for (int i = 0; i < 3; ++i)
            shown[i] = LinearToSrgb(std::max(linearColor[i], 0.0f));

        if (channels == 4)
            shown[3] = linearColor[3];

        // Only the swatch, with the numbers in its popup, as four fields don't fit a narrow panel
        const bool edited = channels == 4
            ? ImGui::ColorEdit4(label, shown.data(), ImGuiColorEditFlags_NoInputs)
            : ImGui::ColorEdit3(label, shown.data(), ImGuiColorEditFlags_NoInputs);

        if (!edited)
            return false;

        for (int i = 0; i < 3; ++i)
            linearColor[i] = SrgbToLinear(shown[i]);

        if (channels == 4)
            linearColor[3] = shown[3];

        return true;
    }
}
