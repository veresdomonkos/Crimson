#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Compiles VSMain and PSMain from HLSL source to SPIR-V, and PSDepth if the source declares a "void PSDepth(".
    // Includes resolve relative to includeDirectory,
    // then to the engine's shaders, and the name is what errors report. Errors go to the log.
    // includedFiles gets the files read from includeDirectory, even when it fails, as fixing one of them can fix it.
    [[nodiscard]] std::optional<ShaderBinary> CompileHlslShader(const std::string& name, const std::string& source, const std::filesystem::path& includeDirectory,
                                                                std::vector<std::filesystem::path>* includedFiles = nullptr);

    // Reads the annotations of the source itself, not of what it includes, by the name of what they follow.
    // The ones it can't understand go to the log.
    [[nodiscard]] std::unordered_map<std::string, ShaderAnnotation> ParseShaderAnnotations(std::string_view source);
}
