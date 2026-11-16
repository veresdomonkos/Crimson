#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/core/hash.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/shader_cache.hpp>
#include <crimson/renderer/shader_compiler.hpp>

#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kMinimalShader = R"(
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)";

    // PSDepth and annotations, to have everything a binary holds
    constexpr std::string_view kFullShader = R"(
cbuffer MaterialBlock : register(b0, space1)
{
    float Cutoff; // @default 0.5 @range 0 1
};
Texture2D Mask : register(t0, space1);

float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(Cutoff, 1.0, 1.0, 1.0); }
void PSDepth(float4 position : SV_Position) { clip(Mask.Load(int3(0, 0, 0)).a - Cutoff); }
)";

    constexpr std::string_view kHelperShader = R"(
#include "helper.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, HELPER_VALUE); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)";

    constexpr std::string_view kEngineIncludeShader = R"(
#include "crimson/math.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, PI); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)";

    ShaderCacheStats Since(const ShaderCacheStats& before)
    {
        const ShaderCacheStats now = ShaderCache::Stats();
        return { now.Compiled - before.Compiled, now.Reused - before.Reused };
    }

    std::optional<ShaderBinary> Compile(ShaderCache& cache, std::string_view source, const fs::path& directory = {},
                                        std::vector<fs::path>* includedFiles = nullptr)
    {
        return CompileHlslShader("cached", std::string(source), directory, includedFiles, &cache);
    }

    void CheckSame(const ShaderBinary& a, const ShaderBinary& b)
    {
        CHECK(a.Vertex == b.Vertex);
        CHECK(a.Pixel == b.Pixel);
        CHECK(a.PixelDepth == b.PixelDepth);
        CHECK(a.VertexInterface == b.VertexInterface);
        CHECK(a.PixelInterface == b.PixelInterface);
        CHECK(a.PixelDepthInterface == b.PixelDepthInterface);
        CHECK(a.Annotations.size() == b.Annotations.size());

        for (const auto& [name, annotation] : a.Annotations)
        {
            const auto found = b.Annotations.find(name);
            REQUIRE(found != b.Annotations.end());
            CHECK(annotation.Default == found->second.Default);
            CHECK(annotation.Range == found->second.Range);
            CHECK(annotation.Texture == found->second.Texture);
        }
    }

    // The one entry a cache folder holds
    fs::path OnlyEntry(const fs::path& folder)
    {
        std::vector<fs::path> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder))
            files.push_back(entry.path());

        REQUIRE(files.size() == 1);
        CHECK(files[0].extension() == ".bin");
        return files[0];
    }

    std::string ReadAll(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    void WriteAll(const fs::path& file, const std::string& bytes)
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // Puts a huge count in the payload, and makes the checksum match it
    void SetPayloadCount(std::string& bytes, std::size_t offsetInPayload)
    {
        constexpr std::size_t kChecksumOffset = sizeof(uint32_t) * 2 + sizeof(uint64_t) * 2;
        constexpr std::size_t kPayloadOffset = kChecksumOffset + sizeof(uint64_t);

        const uint64_t huge = ~uint64_t(0) / 2;
        std::memcpy(bytes.data() + kPayloadOffset + offsetInPayload, &huge, sizeof(huge));

        const uint64_t checksum = Fnv1a(std::string_view(bytes).substr(kPayloadOffset));
        std::memcpy(bytes.data() + kChecksumOffset, &checksum, sizeof(checksum));
    }
}

TEST_CASE("A second compile of the same shader is reused", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path());

    const ShaderCacheStats before = ShaderCache::Stats();
    const auto first = Compile(cache, kFullShader);
    REQUIRE(first);
    REQUIRE_FALSE(first->PixelDepth.empty());
    REQUIRE(first->Annotations.contains("Cutoff"));

    const auto second = Compile(cache, kFullShader);
    REQUIRE(second);

    const ShaderCacheStats change = Since(before);
    CHECK(change.Compiled == 1);
    CHECK(change.Reused == 1);
    CheckSame(*first, *second);
}

TEST_CASE("The memory layer needs no disk", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path() / "cache");

    REQUIRE(Compile(cache, kMinimalShader));
    fs::remove_all(temp.Path() / "cache");

    const ShaderCacheStats before = ShaderCache::Stats();
    CHECK(Compile(cache, kMinimalShader));
    CHECK(Since(before).Reused == 1);
    CHECK(Since(before).Compiled == 0);
}

TEST_CASE("A new process reads the disk cache", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;

    {
        ShaderCache cache(temp.Path());
        REQUIRE(Compile(cache, kFullShader));
    }

    ShaderCache::ForgetMemory();
    ShaderCache fresh(temp.Path());

    const ShaderCacheStats before = ShaderCache::Stats();
    const auto binary = Compile(fresh, kFullShader);
    REQUIRE(binary);
    CHECK(Since(before).Reused == 1);
    CHECK(Since(before).Compiled == 0);
    CHECK_FALSE(binary->Vertex.empty());
    CHECK(binary->Annotations.contains("Cutoff"));
}

TEST_CASE("Changing the source compiles again", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path());

    REQUIRE(Compile(cache, kMinimalShader));

    const ShaderCacheStats before = ShaderCache::Stats();
    CHECK(Compile(cache, std::string(kMinimalShader) + " "));
    CHECK(Since(before).Compiled == 1);
    CHECK(Since(before).Reused == 0);
}

TEST_CASE("Changing an include in the shader's folder compiles again", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    temp.Write("shaders/helper.hlsli", "#define HELPER_VALUE 1.0\n");
    ShaderCache cache(temp.Path() / "cache");

    std::vector<fs::path> compiledFiles;
    REQUIRE(Compile(cache, kHelperShader, temp.Path() / "shaders", &compiledFiles));
    REQUIRE(compiledFiles.size() == 1);

    std::vector<fs::path> reusedFiles;
    const ShaderCacheStats before = ShaderCache::Stats();
    REQUIRE(Compile(cache, kHelperShader, temp.Path() / "shaders", &reusedFiles));
    CHECK(Since(before).Reused == 1);
    CHECK(reusedFiles == compiledFiles);

    temp.Write("shaders/helper.hlsli", "#define HELPER_VALUE 2.0\n");
    ShaderCache::ForgetMemory();

    const ShaderCacheStats beforeChange = ShaderCache::Stats();
    CHECK(Compile(cache, kHelperShader, temp.Path() / "shaders"));
    CHECK(Since(beforeChange).Compiled == 1);
    CHECK(Since(beforeChange).Reused == 0);
}

TEST_CASE("A project file shadowing an engine header compiles again", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path() / "cache");

    REQUIRE(Compile(cache, kEngineIncludeShader, temp.Path() / "shaders"));

    temp.Write("shaders/crimson/math.hlsli", "static const float PI = 3.0;\n");

    const ShaderCacheStats before = ShaderCache::Stats();
    CHECK(Compile(cache, kEngineIncludeShader, temp.Path() / "shaders"));
    CHECK(Since(before).Compiled == 1);
    CHECK(Since(before).Reused == 0);
}

TEST_CASE("A changed engine header compiles again", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path());

    // What an entry looks like that was made with another version of the header
    const uint64_t key = ShaderCacheKey("cached", std::string(kEngineIncludeShader), ShaderCompilerVersion());
    cache.Store(key, { { { "crimson/math.hlsli", true, 12345 } }, {} });

    const ShaderCacheStats before = ShaderCache::Stats();
    CHECK(Compile(cache, kEngineIncludeShader));
    CHECK(Since(before).Compiled == 1);
    CHECK(Since(before).Reused == 0);
}

TEST_CASE("The compiler version and the format are part of the key", "[shader cache]")
{
    CHECK(ShaderCacheKey("a", "b", "compiler 1") == ShaderCacheKey("a", "b", "compiler 1"));
    CHECK(ShaderCacheKey("a", "b", "compiler 1") != ShaderCacheKey("a", "b", "compiler 2"));
    CHECK(ShaderCacheKey("a", "b", "v", 1) != ShaderCacheKey("a", "b", "v", 2));
    CHECK(ShaderCacheKey("a", "b", "v") != ShaderCacheKey("c", "b", "v"));
    CHECK(ShaderCacheKey("a", "b", "v") != ShaderCacheKey("a", "c", "v"));

    // Text moving from one part to the next
    CHECK(ShaderCacheKey("ab", "c", "v") != ShaderCacheKey("a", "bc", "v"));
}

TEST_CASE("A broken cache file is ignored and replaced", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path());

    const LogCounter errors(LogLevel::Error, "");

    REQUIRE(Compile(cache, kFullShader));
    const fs::path file = OnlyEntry(temp.Path());

    const auto truncate = [](std::string& bytes) { bytes.resize(bytes.size() / 2); };
    const auto garbage = [](std::string& bytes) { bytes.assign(bytes.size(), 'x'); };
    const auto oldFormat = [](std::string& bytes)
    {
        const uint32_t old = kShaderCacheFormat - 1;
        std::memcpy(bytes.data() + sizeof(uint32_t), &old, sizeof(old));
    };

    // The header and checksum are right, so it is the payload's own counts that have to be refused. This shader has no
    // includes, so the payload is their count and then the first stage's word count.
    const auto hugeIncludeCount = [](std::string& bytes) { SetPayloadCount(bytes, 0); };
    const auto hugeWordCount = [](std::string& bytes) { SetPayloadCount(bytes, sizeof(uint64_t)); };

    for (const auto& damage : { +truncate, +garbage, +oldFormat, +hugeIncludeCount, +hugeWordCount })
    {
        std::string bytes = ReadAll(file);
        damage(bytes);

        ShaderCache::ForgetMemory();
        WriteAll(file, bytes);

        const ShaderCacheStats before = ShaderCache::Stats();
        CHECK(Compile(cache, kFullShader));
        CHECK(Since(before).Compiled == 1);
        CHECK(Since(before).Reused == 0);

        ShaderCache::ForgetMemory();

        const ShaderCacheStats beforeReuse = ShaderCache::Stats();
        CHECK(Compile(cache, kFullShader));
        CHECK(Since(beforeReuse).Compiled == 0);
        CHECK(Since(beforeReuse).Reused == 1);
    }

    CHECK(errors.Count() == 0);
}

TEST_CASE("An unknown annotation warns on a cache hit too", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path());

    constexpr std::string_view source = R"(
cbuffer MaterialBlock : register(b0, space1)
{
    float Amount; // @bogus 1
};
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(Amount, 1.0, 1.0, 1.0); }
)";

    const LogCounter warnings(LogLevel::Warn, "can't be understood");
    const ShaderCacheStats before = ShaderCache::Stats();

    REQUIRE(Compile(cache, source));
    CHECK(warnings.Count() == 1);

    REQUIRE(Compile(cache, source));
    CHECK(warnings.Count() == 2);
    CHECK(Since(before).Reused == 1);
}

TEST_CASE("A failed compile isn't cached", "[shader cache]")
{
    ShaderCache::ForgetMemory();
    const TempFolder temp;
    ShaderCache cache(temp.Path());

    const LogCounter errors(LogLevel::Error, "[Shader]");
    const ShaderCacheStats before = ShaderCache::Stats();

    CHECK_FALSE(Compile(cache, "this does not compile"));
    const int afterFirst = errors.Count();
    CHECK(afterFirst > 0);

    CHECK_FALSE(Compile(cache, "this does not compile"));
    CHECK(errors.Count() > afterFirst);

    CHECK(Since(before).Compiled == 0);
    CHECK(Since(before).Reused == 0);
    CHECK(fs::is_empty(temp.Path()));
}
