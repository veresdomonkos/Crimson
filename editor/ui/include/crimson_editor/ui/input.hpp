#pragma once
#include <string_view>

#include <glm/vec2.hpp>

#include <crimson/core/keys.hpp>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    // What the user is doing, as the UI sees it this frame
    namespace input
    {
        // Once for each press, and again while held if repeat is set
        [[nodiscard]] bool KeyPressed(Key key, bool repeat = false);

        [[nodiscard]] bool MouseClicked(MouseButton button);
        [[nodiscard]] bool MouseDoubleClicked(MouseButton button);
        [[nodiscard]] bool MouseDown(MouseButton button);
            [[nodiscard]] glm::vec2 MousePosition();
        [[nodiscard]] float MouseWheel();
        [[nodiscard]] KeyModifiers Modifiers();

        // Any field, slider or drag is being used
        [[nodiscard]] bool IsEditingItem();
        [[nodiscard]] bool IsAnyItemHovered();

        // Seconds since the UI started, and since the last frame
        [[nodiscard]] double Time();
        [[nodiscard]] float DeltaTime();

        void SetClipboard(std::string_view text);
    }

    // The item drawn last
    namespace item
    {
        [[nodiscard]] bool Hovered(bool whenDisabled = false);
        [[nodiscard]] bool Clicked(MouseButton button = MouseButton::Left);

        // Hovered, with a double click of the left button
        [[nodiscard]] bool DoubleClicked();
        [[nodiscard]] bool Active();
        [[nodiscard]] bool Deactivated();
        [[nodiscard]] bool DeactivatedAfterEdit();

        // A tree node that was opened or closed by this click
        [[nodiscard]] bool ToggledOpen();

        // Some of it is on screen, which tells what is worth loading
        [[nodiscard]] bool Visible();
        [[nodiscard]] Rect Bounds();
    }
}
