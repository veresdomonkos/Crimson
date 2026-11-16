#include "crimson/asset_manager/import_cache.hpp"

#include <atomic>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <system_error>
#include <utility>

#include "crimson/core/engine_resources.hpp"
#include "crimson/core/hash.hpp"
#include "crimson/core/log.hpp"

#include "asset_manager/binary_io.hpp"
#include <crimson/core/file_write.hpp>

namespace fs = std::filesystem;

namespace crimson
{
    namespace
    {
        constexpr uint32_t kMagic = 0x43495243; // "CRIC" read as bytes on a little-endian machine

        // magic, format, key, payload size and the payload's checksum
        constexpr std::size_t kHeaderSize = sizeof(uint32_t) * 2 + sizeof(uint64_t) * 3;

        // A dependency as it is kept: a File one under the root has its path relative to it
        struct StoredDependency
        {
            ImportDependency::Origin From = ImportDependency::Origin::File;
            bool Relative = false;
            std::string Path;
            std::optional<uint64_t> Hash;
        };

        struct Entry
        {
            std::vector<StoredDependency> Dependencies;
            std::string Bytes;
        };

        struct Memory
        {
            std::mutex Mutex;
            std::map<std::pair<std::string, uint64_t>, std::shared_ptr<const Entry>> Entries;
        };

        Memory& GetMemory()
        {
            static Memory memory;
            return memory;
        }

        std::atomic<std::size_t> s_hits = 0;
        std::atomic<std::size_t> s_stores = 0;

        std::string Serialize(uint64_t key, const Entry& entry)
        {
            std::string payload;
            Append<uint64_t>(payload, entry.Dependencies.size());

            for (const StoredDependency& dependency : entry.Dependencies)
            {
                Append<uint8_t>(payload, static_cast<uint8_t>(dependency.From));
                Append<uint8_t>(payload, dependency.Relative ? 1 : 0);
                AppendString(payload, dependency.Path);
                Append<uint8_t>(payload, dependency.Hash ? 1 : 0);
                Append<uint64_t>(payload, dependency.Hash.value_or(0));
            }

            AppendString(payload, entry.Bytes);

            std::string bytes;
            bytes.reserve(kHeaderSize + payload.size());
            Append<uint32_t>(bytes, kMagic);
            Append<uint32_t>(bytes, kImportCacheFormat);
            Append<uint64_t>(bytes, key);
            Append<uint64_t>(bytes, payload.size());
            Append<uint64_t>(bytes, Fnv1a(payload));
            bytes.append(payload);
            return bytes;
        }

        std::shared_ptr<const Entry> Deserialize(uint64_t key, std::string_view bytes)
        {
            Reader header(bytes);
            uint32_t magic = 0;
            uint32_t format = 0;
            uint64_t storedKey = 0;
            uint64_t payloadSize = 0;
            uint64_t checksum = 0;

            if (!header.Read(magic) || !header.Read(format) || !header.Read(storedKey) || !header.Read(payloadSize) || !header.Read(checksum))
                return nullptr;

            if (magic != kMagic || format != kImportCacheFormat || storedKey != key || payloadSize != bytes.size() - kHeaderSize)
                return nullptr;

            const std::string_view payload = bytes.substr(kHeaderSize);
            if (Fnv1a(payload) != checksum)
                return nullptr;

            Reader reader(payload);
            auto entry = std::make_shared<Entry>();

            uint64_t count = 0;
            if (!reader.Read(count))
                return nullptr;

            // Each dependency takes more than a byte, so a count above the bytes left is corrupt
            if (count > reader.Remaining())
                return nullptr;

            for (uint64_t i = 0; i < count; ++i)
            {
                StoredDependency dependency;
                uint8_t from = 0;
                uint8_t relative = 0;
                uint8_t hasHash = 0;
                uint64_t hash = 0;

                if (!reader.Read(from) || !reader.Read(relative) || !reader.ReadString(dependency.Path) || !reader.Read(hasHash) || !reader.Read(hash))
                    return nullptr;

                if (from > static_cast<uint8_t>(ImportDependency::Origin::Engine))
                    return nullptr;

                dependency.From = static_cast<ImportDependency::Origin>(from);
                dependency.Relative = relative != 0;
                if (hasHash != 0)
                    dependency.Hash = hash;

                entry->Dependencies.push_back(std::move(dependency));
            }

            if (!reader.ReadString(entry->Bytes) || !reader.AtEnd())
                return nullptr;

            return entry;
        }

        fs::path FileOf(const fs::path& folder, std::string_view kind, uint64_t key)
        {
            return folder / kind / std::format("{:016x}.bin", key);
        }

        std::optional<std::string> ReadFile(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return std::nullopt;

            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        fs::path ResolvedPath(const StoredDependency& dependency, const fs::path& root)
        {
            return dependency.Relative ? root / fs::path(dependency.Path) : fs::path(dependency.Path);
        }

        // Whether the dependency is still what the import read
        bool Unchanged(const StoredDependency& dependency, const fs::path& root)
        {
            if (dependency.From == ImportDependency::Origin::Engine)
            {
                const EmbeddedFile* file = FindEmbeddedFile(GetEngineResources(), dependency.Path);
                return file && dependency.Hash && Fnv1a(file->Text()) == *dependency.Hash;
            }

            const fs::path path = ResolvedPath(dependency, root);

            if (!dependency.Hash)
            {
                const std::ifstream file(path, std::ios::binary);
                return !file;
            }

            const auto contents = ReadFile(path);
            return contents && Fnv1a(*contents) == *dependency.Hash;
        }

        // Stays absolute when it isn't inside the root, as it can't move with it
        StoredDependency Stored(const ImportDependency& dependency, const fs::path& root)
        {
            StoredDependency stored{ dependency.From, false, dependency.Path, dependency.Hash };

            if (dependency.From == ImportDependency::Origin::File && !root.empty())
            {
                const fs::path relative = fs::path(dependency.Path).lexically_relative(root);
                if (!relative.empty() && *relative.begin() != "..")
                {
                    stored.Relative = true;
                    stored.Path = relative.generic_string();
                }
            }

            return stored;
        }

        // The hit if every dependency is unchanged
        std::optional<ImportCacheHit> CheckedHit(const Entry& entry, const fs::path& root)
        {
            ImportCacheHit hit;

            for (const StoredDependency& dependency : entry.Dependencies)
            {
                if (!Unchanged(dependency, root))
                    return std::nullopt;

                if (dependency.From == ImportDependency::Origin::File && dependency.Hash)
                    hit.Files.push_back(ResolvedPath(dependency, root));
            }

            hit.Bytes = entry.Bytes;
            ++s_hits;
            return hit;
        }
    }

    ImportCache::ImportCache(fs::path folder, fs::path root)
        : m_folder(std::move(folder)), m_root(std::move(root))
    {
    }

    void ImportCache::SetFolder(fs::path folder)
    {
        m_folder = std::move(folder);
    }

    std::optional<ImportCacheHit> ImportCache::Find(std::string_view kind, uint64_t key) const
    {
        Memory& memory = GetMemory();
        const std::pair<std::string, uint64_t> id(kind, key);

        std::shared_ptr<const Entry> found;
        {
            const std::scoped_lock lock(memory.Mutex);
            if (const auto it = memory.Entries.find(id); it != memory.Entries.end())
                found = it->second;
        }

        if (found)
            return CheckedHit(*found, m_root);

        if (m_folder.empty())
            return std::nullopt;

        const auto bytes = ReadFile(FileOf(m_folder, kind, key));
        if (!bytes)
            return std::nullopt;

        const auto entry = Deserialize(key, *bytes);
        if (!entry)
            return std::nullopt;

        {
            const std::scoped_lock lock(memory.Mutex);
            memory.Entries.insert_or_assign(id, entry);
        }

        return CheckedHit(*entry, m_root);
    }

    void ImportCache::Store(std::string_view kind, uint64_t key, std::span<const ImportDependency> dependencies, std::string_view bytes)
    {
        auto entry = std::make_shared<Entry>();
        entry->Bytes = std::string(bytes);
        for (const ImportDependency& dependency : dependencies)
            entry->Dependencies.push_back(Stored(dependency, m_root));

        ++s_stores;

        {
            Memory& memory = GetMemory();
            const std::scoped_lock lock(memory.Mutex);
            memory.Entries.insert_or_assign(std::pair<std::string, uint64_t>(kind, key), entry);
        }

        if (m_folder.empty())
            return;

        std::error_code error;
        fs::create_directories(m_folder / kind, error);

        // A suffix of its own, as another process can be writing the same entry
        const std::string suffix = std::format(".{:08x}.saving", std::random_device{}());
        if (error || !WriteFileSafely(FileOf(m_folder, kind, key), Serialize(key, *entry), suffix))
            LOG_WARN("[ImportCache] Couldn't write to '{}'", (m_folder / kind).string());
    }

    ImportCache& ImportCache::Engine()
    {
        static ImportCache cache;
        return cache;
    }

    ImportCacheStats ImportCache::Stats()
    {
        return { s_hits.load(), s_stores.load() };
    }

    void ImportCache::ForgetMemory()
    {
        Memory& memory = GetMemory();
        const std::scoped_lock lock(memory.Mutex);
        memory.Entries.clear();
    }
}
