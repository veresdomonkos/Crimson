#pragma once
#include <imgui.h>

#include <crimson/core/keys.hpp>

namespace crimson::editor::ui
{
    // What ImGui calls a key, or ImGuiKey_None for one it has no name for
    ImGuiKey ToImGuiKey(Key key);
}
