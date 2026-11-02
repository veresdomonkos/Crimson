#pragma once
#include <cmath>

namespace crimson::editor::ui
{
    [[nodiscard]] inline float LinearToSrgb(float value)
    {
        return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }

    [[nodiscard]] inline float SrgbToLinear(float value)
    {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    // A color swatch for a value in linear light. It shows what the color looks like on the display,
    // as the renderer encodes it for that but ImGui doesn't. Channels is 3 or 4, alpha is already linear.
    // Returns true when it changed, and only then touches the value.
    bool LinearColorEdit(const char* label, float* linearColor, int channels);
}
