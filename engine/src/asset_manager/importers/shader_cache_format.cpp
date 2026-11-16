#include "asset_manager/importers/shader_cache_format.hpp"

#include <cstring>

#include "crimson/core/hash.hpp"

#include "asset_manager/binary_io.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static void AppendWords(std::string& bytes, const std::vector<uint32_t>& words)
    {
        AppendString(bytes, std::string_view(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t)));
    }

    // The size is checked against what is left before anything is allocated, as it can be anything
    static bool ReadWords(Reader& reader, std::vector<uint32_t>& words)
    {
        std::string bytes;
        if (!reader.ReadString(bytes) || bytes.size() % sizeof(uint32_t) != 0)
            return false;

        words.resize(bytes.size() / sizeof(uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        return true;
    }

    uint64_t ShaderCacheKey(std::string_view name, std::string_view source, std::string_view compilerVersion, uint32_t format)
    {
        // Each part's size goes in with it, so that text moving from one to the next is a different key
        uint64_t hash = Fnv1a(std::string_view(reinterpret_cast<const char*>(&format), sizeof(format)));

        for (const std::string_view part : { name, source, compilerVersion })
        {
            const uint64_t size = part.size();
            hash = Fnv1a(std::string_view(reinterpret_cast<const char*>(&size), sizeof(size)), hash);
            hash = Fnv1a(part, hash);
        }

        return hash;
    }

    std::string WriteShaderBinary(const ShaderBinary& binary)
    {
        std::string bytes;
        for (const std::vector<uint32_t>* words : { &binary.Vertex, &binary.Pixel, &binary.PixelDepth,
                                                    &binary.VertexInterface, &binary.PixelInterface, &binary.PixelDepthInterface })
            AppendWords(bytes, *words);

        return bytes;
    }

    std::optional<ShaderBinary> ReadShaderBinary(std::string_view bytes)
    {
        Reader reader(bytes);
        ShaderBinary binary;

        for (std::vector<uint32_t>* words : { &binary.Vertex, &binary.Pixel, &binary.PixelDepth,
                                              &binary.VertexInterface, &binary.PixelInterface, &binary.PixelDepthInterface })
        {
            if (!ReadWords(reader, *words))
                return std::nullopt;
        }

        if (!reader.AtEnd())
            return std::nullopt;

        return binary;
    }

    std::vector<ImportDependency> ShaderDependencies(std::span<const ShaderInclude> includes, const fs::path& includeDirectory)
    {
        std::vector<ImportDependency> dependencies;

        for (const ShaderInclude& include : includes)
        {
            if (include.EnginePath.empty())
            {
                dependencies.push_back({ ImportDependency::Origin::File, include.File.generic_string(), Fnv1a(include.Text) });
                continue;
            }

            dependencies.push_back({ ImportDependency::Origin::Engine, include.EnginePath, Fnv1a(include.Text) });

            if (!includeDirectory.empty())
                dependencies.push_back({ ImportDependency::Origin::File, (includeDirectory / include.Name).generic_string(), std::nullopt });
        }

        return dependencies;
    }
}
