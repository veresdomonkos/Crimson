#pragma once
#include <optional>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Also checks the frame and view spaces against the engine's structs, failing with a logged error on any mismatch
    [[nodiscard]] std::optional<ShaderInterface> ReflectShaderInterface(const ShaderBinary& binary);
}
