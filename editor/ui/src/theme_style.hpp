#pragma once
#include <imgui.h>

#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor::ui::theme
{
    // Sets the ImGui flags, fonts and colors. Call once, before the first ImGui frame. The scale is the monitor's
    // content scale, 1 on a screen at 100%.
    void Apply(float scale = 1.0f);

    // The font of a weight, loaded by Apply
    [[nodiscard]] ImFont* FontOf(Font font);
}
