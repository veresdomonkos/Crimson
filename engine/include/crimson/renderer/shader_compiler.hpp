#pragma once
#include <filesystem>
#include <optional>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Compiles VSMain and PSMain from an HLSL file to SPIR-V. Includes resolve relative to the file's folder.
    // Errors go to the log.
    [[nodiscard]] std::optional<ShaderBinary> CompileHlslShader(const std::filesystem::path& file);
}
