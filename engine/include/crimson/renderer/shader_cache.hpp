#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "crimson/renderer/shader_interface.hpp"

namespace crimson
{
    // Bump it when the file layout changes, and with the pins of glslang and SPIRV-Tools,
    // as their reported versions can stay the same between two commits.
    inline constexpr uint32_t kShaderCacheFormat = 1;

    // An include as it was resolved for one compile
    struct ShaderInclude
    {
        std::string Name; // as written in #include
        bool FromEngine = false; // the engine's embedded file, not the shader's folder
        uint64_t Hash = 0; // Fnv1a of its text

        bool operator==(const ShaderInclude&) const = default;
    };

    // Binary.Annotations is never stored, as they are parsed from the source on every load. It is empty when found.
    struct ShaderCacheEntry
    {
        std::vector<ShaderInclude> Includes;
        ShaderBinary Binary;
    };

    struct ShaderCacheStats
    {
        std::size_t Compiled = 0;
        std::size_t Reused = 0;
    };

    // Everything of the input that the includes aren't: the name, the source, the compiler and the file format
    [[nodiscard]] uint64_t ShaderCacheKey(std::string_view name, std::string_view source, std::string_view compilerVersion,
                                          uint32_t format = kShaderCacheFormat);

    // Compiled shaders kept in memory for the process, and in a folder for the next one. The folder can always be made
    // again. The memory layer is shared by every cache, and all of it is safe to use from several threads.
    class ShaderCache
    {
    public:
        explicit ShaderCache(std::filesystem::path folder = {}); // empty: only the memory layer
        void SetFolder(std::filesystem::path folder);
        [[nodiscard]] const std::filesystem::path& Folder() const { return m_folder; }

        // Memory first, then <folder>/<key as 16 hex digits>.bin. A missing, unreadable or broken file is nothing.
        [[nodiscard]] std::optional<ShaderCacheEntry> Find(uint64_t key) const;

        // Memory, and the folder if there is one, written atomically. A failed write is only a warning.
        void Store(uint64_t key, const ShaderCacheEntry& entry);

        static ShaderCache& Engine(); // the built-in shaders' cache, memory only until SetFolder
        static ShaderCacheStats Stats(); // for the whole process, counted by CompileHlslShader
        static void ForgetMemory(); // as a new process would start

        // For CompileHlslShader to say what it did
        static void CountCompiled();
        static void CountReused();

    private:
        std::filesystem::path m_folder;
    };
}
