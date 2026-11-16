#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crimson/asset_manager/import_cache.hpp"
#include "crimson/renderer/shader_compiler.hpp"
#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Bump it when the serialized binary changes, and with the pins of glslang and SPIRV-Tools,
    // as their reported versions can stay the same between two commits.
    inline constexpr uint32_t kShaderCacheFormat = 1;

    // The subfolder of the import cache
    inline constexpr std::string_view kShaderCacheKind = "shaders";

    // Everything of the input that the includes aren't: the name, the source, the compiler and the format
    [[nodiscard]] uint64_t ShaderCacheKey(std::string_view name, std::string_view source,
                                          std::string_view compilerVersion = ShaderCompilerVersion(),
                                          uint32_t format = kShaderCacheFormat);

    // The stages and their interfaces. Annotations aren't written, as they are parsed from the source on every load.
    [[nodiscard]] std::string WriteShaderBinary(const ShaderBinary& binary);

    // Nothing if the bytes aren't what WriteShaderBinary made
    [[nodiscard]] std::optional<ShaderBinary> ReadShaderBinary(std::string_view bytes);

    // What a compile read, as dependencies. An engine include also depends on the folder not having a file of that name,
    // as one that shadows it would be read instead. The paths are absolute, for the cache to make them relative.
    [[nodiscard]] std::vector<ImportDependency> ShaderDependencies(std::span<const ShaderInclude> includes,
                                                                   const std::filesystem::path& includeDirectory);
}
