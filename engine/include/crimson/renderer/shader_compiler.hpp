#pragma once
#include <filesystem>
#include <optional>
#include <string>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Compiles VSMain and PSMain from HLSL source to SPIR-V. Includes resolve relative to includeDirectory,
    // and the name is what errors report. Errors go to the log.
    [[nodiscard]] std::optional<ShaderBinary> CompileHlslShader(const std::string& name, const std::string& source, const std::filesystem::path& includeDirectory);
}
