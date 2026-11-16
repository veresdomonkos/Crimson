#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crimson
{
    // The file layout's own version, not an importer's
    inline constexpr uint32_t kImportCacheFormat = 1;

    // A file an import read, checked again on every hit
    struct ImportDependency
    {
        enum class Origin : uint8_t { File, Engine };

        Origin From = Origin::File;
        std::string Path; // File: the absolute path, generic form. Engine: the embedded path, e.g. "shaders/crimson/math.hlsli"
        std::optional<uint64_t> Hash; // Fnv1a of its contents. None: the import looked for this file and it wasn't there
    };

    struct ImportCacheHit
    {
        std::string Bytes; // what the importer stored
        std::vector<std::filesystem::path> Files; // the File dependencies that exist, absolute, for ImportContext::Dependencies
    };

    struct ImportCacheStats
    {
        std::size_t Hits = 0;
        std::size_t Stores = 0;
    };

    // What imports made, kept in memory for the process and in a folder for the next one, so that an importer only has to
    // give a key and its bytes. The folder can always be made again. The memory layer is shared by every cache, and all
    // of it is safe to use from several threads.
    class ImportCache
    {
    public:
        // Entries go in folder (empty: memory only). File dependencies under root are kept relative to it,
        // so the cache still holds when the folder they are in moves.
        explicit ImportCache(std::filesystem::path folder = {}, std::filesystem::path root = {});
        void SetFolder(std::filesystem::path folder);
        [[nodiscard]] const std::filesystem::path& Folder() const { return m_folder; }

        // kind is the importer's subfolder ("shaders"). Memory first, then <folder>/<kind>/<key as 16 hex digits>.bin.
        // A missing, broken or old file, or any dependency that changed, is a miss.
        [[nodiscard]] std::optional<ImportCacheHit> Find(std::string_view kind, uint64_t key) const;

        // Memory, and the folder if there is one, written atomically. A failed write is only a warning.
        void Store(std::string_view kind, uint64_t key, std::span<const ImportDependency> dependencies, std::string_view bytes);

        static ImportCache& Engine(); // the engine's own assets: no root, memory only until SetFolder
        static ImportCacheStats Stats(); // for the whole process
        static void ForgetMemory(); // as a new process would start

    private:
        std::filesystem::path m_folder;
        std::filesystem::path m_root;
    };
}
