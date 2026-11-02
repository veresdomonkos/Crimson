#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Compiles VSMain and PSMain from HLSL source to SPIR-V. Includes resolve relative to includeDirectory,
    // then to the engine's shaders, and the name is what errors report. Errors go to the log.
    [[nodiscard]] std::optional<ShaderBinary> CompileHlslShader(const std::string& name, const std::string& source, const std::filesystem::path& includeDirectory);

    // Reads the annotations of the source itself, not of what it includes, by the name of what they follow.
    // The ones it can't understand go to the log.
    [[nodiscard]] std::unordered_map<std::string, ShaderAnnotation> ParseShaderAnnotations(std::string_view source);
}
