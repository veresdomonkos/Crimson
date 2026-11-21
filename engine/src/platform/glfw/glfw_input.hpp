#pragma once

#include <optional>

#include "crimson/core/keys.hpp"

namespace crimson::glfw
{
    // Nothing for a key the engine has no name for
    [[nodiscard]] std::optional<Key> ToKey(int glfwKey);

    [[nodiscard]] std::optional<MouseButton> ToMouseButton(int glfwButton);

    [[nodiscard]] KeyModifiers ToModifiers(int glfwMods);
}
