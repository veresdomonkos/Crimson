#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/asset_manager/asset_importer.hpp>
#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/import_cache.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

#include <crimson/core/engine_resources.hpp>
#include <crimson/core/hash.hpp>
#include <crimson/core/log.hpp>

#include "support/fake_gpu.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

constexpr std::string_view kKind = "things";
constexpr uint64_t kKey = 0x1234abcd5678ef90ull;

constexpr std::string_view kEngineHeader = "shaders/crimson/math.hlsli";

static ImportCacheStats Since(const ImportCacheStats& before)
{
    const ImportCacheStats now = ImportCache::Stats();
    return { now.Hits - before.Hits, now.Stores - before.Stores };
}

static ImportDependency FileDependency(const fs::path& file, std::string_view contents)
{
    return { ImportDependency::Origin::File, file.generic_string(), Fnv1a(contents) };
}

static ImportDependency MissingFile(const fs::path& file)
{
    return { ImportDependency::Origin::File, file.generic_string(), std::nullopt };
}

static ImportDependency EngineDependency(std::string_view path, uint64_t hash)
{
    return { ImportDependency::Origin::Engine, std::string(path), hash };
}

static std::string ReadAll(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static void WriteAll(const fs::path& file, const std::string& bytes)
{
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The one entry a kind's folder holds
static fs::path OnlyEntry(const fs::path& folder)
{
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder))
        files.push_back(entry.path());

    REQUIRE(files.size() == 1);
    CHECK(files[0].extension() == ".bin");
    return files[0];
}

constexpr std::size_t kKeyOffset = sizeof(uint32_t) * 2;
constexpr std::size_t kChecksumOffset = kKeyOffset + sizeof(uint64_t) * 2;
constexpr std::size_t kPayloadOffset = kChecksumOffset + sizeof(uint64_t);

// Puts a huge count in the payload, and makes the checksum match it
static void SetPayloadCount(std::string& bytes, std::size_t offsetInPayload)
{
    const uint64_t huge = ~uint64_t(0) / 2;
    std::memcpy(bytes.data() + kPayloadOffset + offsetInPayload, &huge, sizeof(huge));

    const uint64_t checksum = Fnv1a(std::string_view(bytes).substr(kPayloadOffset));
    std::memcpy(bytes.data() + kChecksumOffset, &checksum, sizeof(checksum));
}

TEST_CASE("A stored entry is found with its bytes", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    ImportCache cache(temp.Path());

    const ImportCacheStats before = ImportCache::Stats();
    cache.Store(kKind, kKey, {}, "some bytes\0with a zero");

    const auto hit = cache.Find(kKind, kKey);
    REQUIRE(hit);
    CHECK(hit->Bytes == "some bytes\0with a zero");
    CHECK(hit->Files.empty());

    CHECK(Since(before).Stores == 1);
    CHECK(Since(before).Hits == 1);
    CHECK_FALSE(cache.Find(kKind, kKey + 1));
}

TEST_CASE("The memory layer needs no disk", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    ImportCache cache(temp.Path() / "cache");

    cache.Store(kKind, kKey, {}, "bytes");
    fs::remove_all(temp.Path() / "cache");

    const auto hit = cache.Find(kKind, kKey);
    REQUIRE(hit);
    CHECK(hit->Bytes == "bytes");
}

TEST_CASE("A new process reads the folder", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;

    {
        ImportCache cache(temp.Path());
        cache.Store(kKind, kKey, {}, "bytes");
    }

    CHECK(fs::exists(temp.Path() / kKind / "1234abcd5678ef90.bin"));

    ImportCache::ForgetMemory();
    ImportCache fresh(temp.Path());

    const ImportCacheStats before = ImportCache::Stats();
    const auto hit = fresh.Find(kKind, kKey);
    REQUIRE(hit);
    CHECK(hit->Bytes == "bytes");
    CHECK(Since(before).Hits == 1);
    CHECK(Since(before).Stores == 0);
}

TEST_CASE("Kinds don't share entries", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    ImportCache cache(temp.Path());

    cache.Store("a", kKey, {}, "from a");
    cache.Store("b", kKey, {}, "from b");
    CHECK(fs::exists(temp.Path() / "a" / "1234abcd5678ef90.bin"));
    CHECK(fs::exists(temp.Path() / "b" / "1234abcd5678ef90.bin"));

    ImportCache::ForgetMemory();
    REQUIRE(cache.Find("a", kKey));
    REQUIRE(cache.Find("b", kKey));
    CHECK(cache.Find("a", kKey)->Bytes == "from a");
    CHECK(cache.Find("b", kKey)->Bytes == "from b");
    CHECK_FALSE(cache.Find("c", kKey));
}

TEST_CASE("A changed file dependency is a miss", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    const fs::path file = temp.Write("input.txt", "one");
    ImportCache cache(temp.Path() / "cache");

    const ImportDependency dependency = FileDependency(file, "one");
    cache.Store(kKind, kKey, { &dependency, 1 }, "bytes");

    const auto hit = cache.Find(kKind, kKey);
    REQUIRE(hit);
    REQUIRE(hit->Files.size() == 1);
    CHECK(hit->Files[0] == file);

    temp.Write("input.txt", "two");
    CHECK_FALSE(cache.Find(kKind, kKey));

    ImportCache::ForgetMemory();
    CHECK_FALSE(cache.Find(kKind, kKey));

    fs::remove(file);
    CHECK_FALSE(cache.Find(kKind, kKey));
}

TEST_CASE("A file that appears where one was missing is a miss", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    const fs::path file = temp.Path() / "later.txt";
    ImportCache cache(temp.Path() / "cache");

    const ImportDependency dependency = MissingFile(file);
    cache.Store(kKind, kKey, { &dependency, 1 }, "bytes");

    const auto hit = cache.Find(kKind, kKey);
    REQUIRE(hit);
    CHECK(hit->Files.empty());

    temp.Write("later.txt", "now it is there");
    CHECK_FALSE(cache.Find(kKind, kKey));
}

TEST_CASE("A changed engine resource is a miss", "[import cache]")
{
    ImportCache::ForgetMemory();
    const EmbeddedFile* header = FindEmbeddedFile(GetEngineResources(), kEngineHeader);
    REQUIRE(header);

    const TempFolder temp;
    ImportCache cache(temp.Path());

    const ImportDependency wrong = EngineDependency(kEngineHeader, Fnv1a(header->Text()) + 1);
    cache.Store(kKind, kKey, { &wrong, 1 }, "bytes");
    CHECK_FALSE(cache.Find(kKind, kKey));

    const ImportDependency right = EngineDependency(kEngineHeader, Fnv1a(header->Text()));
    cache.Store(kKind, kKey, { &right, 1 }, "bytes");

    const auto hit = cache.Find(kKind, kKey);
    REQUIRE(hit);
    CHECK(hit->Files.empty());

    const ImportDependency gone = EngineDependency("shaders/no/such/header.hlsli", 1);
    cache.Store(kKind, kKey, { &gone, 1 }, "bytes");
    CHECK_FALSE(cache.Find(kKind, kKey));
}

TEST_CASE("File dependencies under the root are kept relative to it", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    const fs::path inside = temp.Write("a/x.txt", "inside");
    const fs::path outside = temp.Write("outside.txt", "outside");

    {
        ImportCache cache(temp.Path() / "a" / "cache", temp.Path() / "a");

        const ImportDependency dependencies[] = { FileDependency(inside, "inside"), FileDependency(outside, "outside") };
        cache.Store(kKind, kKey, dependencies, "bytes");
    }

    const fs::path entry = OnlyEntry(temp.Path() / "a" / "cache" / kKind);
    const std::string bytes = ReadAll(entry);
    CHECK(bytes.find((temp.Path() / "a").generic_string()) == std::string::npos);
    CHECK(bytes.find(outside.generic_string()) != std::string::npos);

    fs::rename(temp.Path() / "a", temp.Path() / "b");
    ImportCache::ForgetMemory();

    ImportCache moved(temp.Path() / "b" / "cache", temp.Path() / "b");
    const auto hit = moved.Find(kKind, kKey);
    REQUIRE(hit);
    REQUIRE(hit->Files.size() == 2);
    CHECK(hit->Files[0] == temp.Path() / "b" / "x.txt");
    CHECK(hit->Files[1] == outside);

    temp.Write("b/x.txt", "changed");
    CHECK_FALSE(moved.Find(kKind, kKey));
}

TEST_CASE("A broken cache file is a miss", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    ImportCache cache(temp.Path());

    const LogCounter errors(LogLevel::Error, "");

    const ImportDependency dependency = EngineDependency(kEngineHeader, Fnv1a(FindEmbeddedFile(GetEngineResources(), kEngineHeader)->Text()));
    cache.Store(kKind, kKey, { &dependency, 1 }, "bytes");
    const fs::path file = OnlyEntry(temp.Path() / kKind);

    const auto truncate = [](std::string& bytes) { bytes.resize(bytes.size() / 2); };
    const auto garbage = [](std::string& bytes) { bytes.assign(bytes.size(), 'x'); };
    const auto oldFormat = [](std::string& bytes)
    {
        const uint32_t old = kImportCacheFormat - 1;
        std::memcpy(bytes.data() + sizeof(uint32_t), &old, sizeof(old));
    };
    const auto wrongKey = [](std::string& bytes)
    {
        const uint64_t other = kKey + 1;
        std::memcpy(bytes.data() + kKeyOffset, &other, sizeof(other));
    };

    // The header and checksum are right, so it is the payload's own count that has to be refused
    const auto hugeDependencyCount = [](std::string& bytes) { SetPayloadCount(bytes, 0); };

    for (const auto& damage : { +truncate, +garbage, +oldFormat, +wrongKey, +hugeDependencyCount })
    {
        std::string bytes = ReadAll(file);
        damage(bytes);

        ImportCache::ForgetMemory();
        WriteAll(file, bytes);
        CHECK_FALSE(cache.Find(kKind, kKey));

        cache.Store(kKind, kKey, { &dependency, 1 }, "bytes");
        ImportCache::ForgetMemory();
        CHECK(cache.Find(kKind, kKey));
    }

    CHECK(errors.Count() == 0);
}

namespace
{
    // An importer with nothing shared with the engine's, to show that caching needs no change to anything else. Its source
    // names a file next to it, which it reads and keeps the length of.
    class CountingImporter final : public AssetImporter<TextureAsset>
    {
    public:
        std::span<const std::string_view> Extensions() const override
        {
            static constexpr std::string_view exts[] = { ".fake" };
            return exts;
        }

        nlohmann::json DefaultSettings() const override { return nlohmann::json::object(); }

        std::optional<TextureHandle> Load(const AssetSource& source, ImportContext& ctx) override
        {
            const auto text = source.Read();
            if (!text)
                return {};

            const fs::path sibling = source.File.parent_path() / *text;
            const uint64_t key = Fnv1a(*text);

            if (const auto hit = ctx.Cache.Find("fake", key))
            {
                ctx.Dependencies.insert(ctx.Dependencies.end(), hit->Files.begin(), hit->Files.end());
                return MakeTexture(ctx);
            }

            ++Imports;
            const std::string contents = ReadAll(sibling);
            ctx.Dependencies.push_back(sibling);

            const ImportDependency dependency = FileDependency(sibling, contents);
            ctx.Cache.Store("fake", key, { &dependency, 1 }, contents);
            return MakeTexture(ctx);
        }

        void Unload(TextureHandle& texture, GpuResourceManager& gpu) override { gpu.DestroyTexture(texture); }

        int Imports = 0;

    private:
        static TextureHandle MakeTexture(ImportContext& ctx)
        {
            TextureInfo info{};
            info.Width = 1;
            info.Height = 1;
            info.Format = TextureFormat::RGBA8;
            info.Usage = TextureUsage::Sampled;
            info.MipLevels = 1;

            const uint32_t pixel = 0;
            return ctx.Gpu.CreateTexture(info, &pixel);
        }
    };
}

TEST_CASE("A fake importer caches through the import context", "[import cache]")
{
    ImportCache::ForgetMemory();
    const TempFolder temp;
    const fs::path sibling = temp.Write("input.txt", "one");
    const fs::path source = temp.Write("thing.fake", "input.txt");

    tests::FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());
    ImportCache cache(temp.Path() / "cache", temp.Path());
    CountingImporter importer;

    AssetInfo info;
    info.Type = AssetType::Of<TextureAsset>();
    info.Path = "thing.fake";
    const AssetSource asset{ info, source, nullptr };

    const auto load = [&]
    {
        ImportContext ctx{ gpu, assets, cache };
        const auto texture = importer.Load(asset, ctx);
        CHECK(texture);
        CHECK(ctx.Dependencies == std::vector{ sibling });
    };

    load();
    CHECK(importer.Imports == 1);

    load();
    CHECK(importer.Imports == 1);

    temp.Write("input.txt", "two");
    load();
    CHECK(importer.Imports == 2);

    load();
    CHECK(importer.Imports == 2);
}
