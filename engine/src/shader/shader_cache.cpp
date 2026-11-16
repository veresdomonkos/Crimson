#include "crimson/renderer/shader_cache.hpp"

#include <atomic>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "crimson/core/hash.hpp"
#include "crimson/core/log.hpp"

#include "asset_manager/file_write.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    namespace
    {
        constexpr uint32_t kMagic = 0x43535243; // "CRSC" read as bytes on a little-endian machine

        // magic, format, key, payload size and the payload's checksum
        constexpr std::size_t kHeaderSize = sizeof(uint32_t) * 2 + sizeof(uint64_t) * 3;

        struct Memory
        {
            std::mutex Mutex;
            std::unordered_map<uint64_t, std::shared_ptr<const ShaderCacheEntry>> Entries;
        };

        Memory& GetMemory()
        {
            static Memory memory;
            return memory;
        }

        std::atomic<std::size_t> s_compiled = 0;
        std::atomic<std::size_t> s_reused = 0;

        template <typename T>
        void Append(std::string& bytes, T value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        void AppendWords(std::string& bytes, const std::vector<uint32_t>& words)
        {
            Append<uint64_t>(bytes, words.size());
            bytes.append(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
        }

        // Reads from bytes without going past them. Once something doesn't fit, everything after it fails too.
        class Reader
        {
        public:
            explicit Reader(std::string_view bytes) : m_bytes(bytes) {}

            template <typename T>
            bool Read(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                if (m_bytes.size() < sizeof(T))
                    return Fail();

                std::memcpy(&value, m_bytes.data(), sizeof(T));
                m_bytes.remove_prefix(sizeof(T));
                return true;
            }

            // The count is checked against what is left before anything is allocated, as it can be anything
            bool ReadString(std::string& text)
            {
                uint64_t size = 0;
                if (!Read(size) || size > m_bytes.size())
                    return Fail();

                text.assign(m_bytes.data(), static_cast<std::size_t>(size));
                m_bytes.remove_prefix(static_cast<std::size_t>(size));
                return true;
            }

            bool ReadWords(std::vector<uint32_t>& words)
            {
                uint64_t count = 0;
                if (!Read(count) || count > m_bytes.size() / sizeof(uint32_t))
                    return Fail();

                words.resize(static_cast<std::size_t>(count));
                std::memcpy(words.data(), m_bytes.data(), words.size() * sizeof(uint32_t));
                m_bytes.remove_prefix(words.size() * sizeof(uint32_t));
                return true;
            }

            [[nodiscard]] bool AtEnd() const { return m_bytes.empty(); }

        private:
            bool Fail()
            {
                m_bytes = {};
                return false;
            }

            std::string_view m_bytes;
        };

        std::string Serialize(uint64_t key, const ShaderCacheEntry& entry)
        {
            std::string payload;
            Append<uint64_t>(payload, entry.Includes.size());

            for (const ShaderInclude& include : entry.Includes)
            {
                Append<uint64_t>(payload, include.Name.size());
                payload.append(include.Name);
                Append<uint8_t>(payload, include.FromEngine ? 1 : 0);
                Append<uint64_t>(payload, include.Hash);
            }

            const ShaderBinary& binary = entry.Binary;
            for (const std::vector<uint32_t>* words : { &binary.Vertex, &binary.Pixel, &binary.PixelDepth,
                                                        &binary.VertexInterface, &binary.PixelInterface, &binary.PixelDepthInterface })
                AppendWords(payload, *words);

            std::string bytes;
            bytes.reserve(kHeaderSize + payload.size());
            Append<uint32_t>(bytes, kMagic);
            Append<uint32_t>(bytes, kShaderCacheFormat);
            Append<uint64_t>(bytes, key);
            Append<uint64_t>(bytes, payload.size());
            Append<uint64_t>(bytes, Fnv1a(payload));
            bytes.append(payload);
            return bytes;
        }

        std::optional<ShaderCacheEntry> Deserialize(uint64_t key, std::string_view bytes)
        {
            Reader header(bytes);
            uint32_t magic = 0;
            uint32_t format = 0;
            uint64_t storedKey = 0;
            uint64_t payloadSize = 0;
            uint64_t checksum = 0;

            if (!header.Read(magic) || !header.Read(format) || !header.Read(storedKey) || !header.Read(payloadSize) || !header.Read(checksum))
                return std::nullopt;

            if (magic != kMagic || format != kShaderCacheFormat || storedKey != key || payloadSize != bytes.size() - kHeaderSize)
                return std::nullopt;

            const std::string_view payload = bytes.substr(kHeaderSize);
            if (Fnv1a(payload) != checksum)
                return std::nullopt;

            Reader reader(payload);
            ShaderCacheEntry entry;

            uint64_t includeCount = 0;
            if (!reader.Read(includeCount))
                return std::nullopt;

            // Each include takes more than a byte, so a count above the bytes left is corrupt
            if (includeCount > payload.size())
                return std::nullopt;

            for (uint64_t i = 0; i < includeCount; ++i)
            {
                ShaderInclude include;
                uint8_t fromEngine = 0;

                if (!reader.ReadString(include.Name) || !reader.Read(fromEngine) || !reader.Read(include.Hash))
                    return std::nullopt;

                include.FromEngine = fromEngine != 0;
                entry.Includes.push_back(std::move(include));
            }

            ShaderBinary& binary = entry.Binary;
            for (std::vector<uint32_t>* words : { &binary.Vertex, &binary.Pixel, &binary.PixelDepth,
                                                  &binary.VertexInterface, &binary.PixelInterface, &binary.PixelDepthInterface })
            {
                if (!reader.ReadWords(*words))
                    return std::nullopt;
            }

            if (!reader.AtEnd())
                return std::nullopt;

            return entry;
        }

        fs::path FileOf(const fs::path& folder, uint64_t key)
        {
            return folder / std::format("{:016x}.bin", key);
        }
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

    ShaderCache::ShaderCache(fs::path folder)
        : m_folder(std::move(folder))
    {
    }

    void ShaderCache::SetFolder(fs::path folder)
    {
        m_folder = std::move(folder);
    }

    std::optional<ShaderCacheEntry> ShaderCache::Find(uint64_t key) const
    {
        Memory& memory = GetMemory();

        {
            const std::scoped_lock lock(memory.Mutex);
            if (const auto found = memory.Entries.find(key); found != memory.Entries.end())
                return *found->second;
        }

        if (m_folder.empty())
            return std::nullopt;

        std::ifstream file(FileOf(m_folder, key), std::ios::binary);
        if (!file)
            return std::nullopt;

        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto entry = Deserialize(key, bytes);
        if (!entry)
            return std::nullopt;

        const std::scoped_lock lock(memory.Mutex);
        memory.Entries.insert_or_assign(key, std::make_shared<const ShaderCacheEntry>(*entry));
        return entry;
    }

    void ShaderCache::Store(uint64_t key, const ShaderCacheEntry& entry)
    {
        // Annotations are parsed from the source on every load
        ShaderCacheEntry stored{ entry.Includes, entry.Binary };
        stored.Binary.Annotations.clear();

        {
            Memory& memory = GetMemory();
            const std::scoped_lock lock(memory.Mutex);
            memory.Entries.insert_or_assign(key, std::make_shared<const ShaderCacheEntry>(stored));
        }

        if (m_folder.empty())
            return;

        std::error_code error;
        fs::create_directories(m_folder, error);

        // A suffix of its own, as another process can be writing the same entry
        const std::string suffix = std::format(".{:08x}.saving", std::random_device{}());
        if (error || !WriteFileSafely(FileOf(m_folder, key), Serialize(key, stored), suffix))
            LOG_WARN("[ShaderCache] Couldn't write to '{}'", m_folder.string());
    }

    ShaderCache& ShaderCache::Engine()
    {
        static ShaderCache cache;
        return cache;
    }

    ShaderCacheStats ShaderCache::Stats()
    {
        return { s_compiled.load(), s_reused.load() };
    }

    void ShaderCache::ForgetMemory()
    {
        Memory& memory = GetMemory();
        const std::scoped_lock lock(memory.Mutex);
        memory.Entries.clear();
    }

    void ShaderCache::CountCompiled()
    {
        ++s_compiled;
    }

    void ShaderCache::CountReused()
    {
        ++s_reused;
    }
}
