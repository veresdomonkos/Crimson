#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // The names of one shader's properties and textures by their PropertyId, to find two that share one
    using ShaderNames = std::unordered_map<PropertyId, std::string, PropertyIdHash>;

    // Adds a name of the shader, which is fine when it is already there. When another name has the same id this reports an
    // error and returns false, and the first name keeps the id. Practically never with 64 bits, but it can't go unnoticed.
    bool AddShaderName(ShaderNames& seen, PropertyId id, std::string_view name);

    // Also checks the frame and view spaces against the engine's structs, failing with a logged error on any mismatch
    [[nodiscard]] std::optional<ShaderInterface> ReflectShaderInterface(const ShaderBinary& binary);
}
