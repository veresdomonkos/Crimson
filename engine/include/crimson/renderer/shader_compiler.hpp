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
    // An include a compile read, each one once, also when the compile failed
    struct ShaderInclude
    {
        std::string Name; // as written in #include
        std::filesystem::path File; // the file read from includeDirectory, empty if it was the engine's
        std::string EnginePath; // "shaders/<Name>" if it was the engine's
        std::string Text; // what was read
    };

    namespace shader_compiler
    {
        // Compiles VSMain and PSMain from HLSL source to SPIR-V, and PSDepth if the source declares a "void PSDepth(".
        // Includes resolve relative to includeDirectory,
        // then to the engine's shaders, and the name is what errors report. Errors go to the log.
        // includes gets what was read, even when it fails, as fixing one of them can fix it.
        [[nodiscard]] std::optional<ShaderBinary> Compile(const std::string& name, const std::string& source, const std::filesystem::path& includeDirectory,
                                                          std::vector<ShaderInclude>* includes = nullptr);

        // What the compilers are and how they are set, for anything that keeps what they make
        [[nodiscard]] std::string_view Version();

        // Reads the annotations of the source itself, not of what it includes, by the name of what they follow.
        // The ones it can't understand go to the log.
        [[nodiscard]] std::unordered_map<std::string, ShaderAnnotation> ParseAnnotations(std::string_view source);
    }
}
