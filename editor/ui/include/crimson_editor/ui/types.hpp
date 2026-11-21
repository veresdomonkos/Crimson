#pragma once
#include <cstdint>
#include <string_view>

#include <glm/vec2.hpp>

namespace crimson::editor::ui
{
    // A color in sRGB, as it is shown
    struct Color
    {
        float R = 0.0f;
        float G = 0.0f;
        float B = 0.0f;
        float A = 1.0f;
    };

    constexpr Color WithAlpha(Color color, float alpha)
    {
        return Color{ color.R, color.G, color.B, alpha };
    }

    // A box on the screen, in pixels
    struct Rect
    {
        glm::vec2 Min{ 0.0f };
        glm::vec2 Max{ 0.0f };

        [[nodiscard]] glm::vec2 Size() const { return Max - Min; }
        [[nodiscard]] glm::vec2 Center() const { return (Min + Max) * 0.5f; }
    };

    // Names a widget, which is how what opened a window finds out what it opened it for. Made from an id in the same place
    // in the window as the widget it is for.
    using WidgetId = std::uint32_t;

    [[nodiscard]] WidgetId IdOf(std::string_view id);
}
