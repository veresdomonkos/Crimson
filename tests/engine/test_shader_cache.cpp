#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/import_cache.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>

#include <crimson/core/log.hpp>
#include <crimson/renderer/shader_compiler.hpp>

#include "asset_manager/importers/shader_cache_format.hpp"

#include "support/fake_gpu.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
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

    ImportCacheStats Since(const ImportCacheStats& before)
    {
        const ImportCacheStats now = ImportCache::Stats();
        return { now.Hits - before.Hits, now.Stores - before.Stores };
    }

    void CheckSame(const ShaderBinary& a, const ShaderBinary& b)
    {
        CHECK(a.Vertex == b.Vertex);
        CHECK(a.Pixel == b.Pixel);
        CHECK(a.PixelDepth == b.PixelDepth);
        CHECK(a.VertexInterface == b.VertexInterface);
        CHECK(a.PixelInterface == b.PixelInterface);
        CHECK(a.PixelDepthInterface == b.PixelDepthInterface);
    }

    // A project with one shader, loaded through a manager of its own each time, as every run of the editor does
    class ShaderProject
    {
    public:
        fs::path Assets() const { return m_temp.Path() / "assets"; }
        fs::path Cache() const { return m_temp.Path() / ".crimson" / "cache"; }

        void Write(const fs::path& relative, std::string_view contents) const { m_temp.Write(Assets() / relative, contents); }

        bool Load() const
        {
            FakeGpuResourceManager gpu;
            AssetManager assets(gpu, Assets(), Cache());
            return static_cast<bool>(assets.Load<ShaderAsset>("mine.hlsl"));
        }

    private:
        TempFolder m_temp;
    };
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

TEST_CASE("A shader binary is read back as it was written", "[shader cache]")
{
    const auto binary = shader_compiler::Compile("full", std::string(kFullShader), {});
    REQUIRE(binary);
    REQUIRE_FALSE(binary->PixelDepth.empty());
    REQUIRE(binary->Annotations.contains("Cutoff"));

    const auto read = ReadShaderBinary(WriteShaderBinary(*binary));
    REQUIRE(read);
    CheckSame(*binary, *read);
    CHECK(read->Annotations.empty());
}

TEST_CASE("A broken shader payload is refused", "[shader cache]")
{
    const auto binary = shader_compiler::Compile("full", std::string(kFullShader), {});
    REQUIRE(binary);

    const std::string bytes = WriteShaderBinary(*binary);
    REQUIRE(ReadShaderBinary(bytes));

    CHECK_FALSE(ReadShaderBinary(bytes.substr(0, bytes.size() / 2)));
    CHECK_FALSE(ReadShaderBinary(bytes.substr(0, bytes.size() - 1)));
    CHECK_FALSE(ReadShaderBinary(bytes + "x"));
    CHECK_FALSE(ReadShaderBinary(""));

    // The first stage's size is more than there is
    std::string hugeSize = bytes;
    const uint64_t huge = ~uint64_t(0) / 2;
    std::memcpy(hugeSize.data(), &huge, sizeof(huge));
    CHECK_FALSE(ReadShaderBinary(hugeSize));
}

TEST_CASE("A second load of the same shader is reused", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;
    project.Write("mine.hlsl", kFullShader);

    const ImportCacheStats before = ImportCache::Stats();
    REQUIRE(project.Load());
    CHECK(Since(before).Stores == 1);
    CHECK(Since(before).Hits == 0);

    const ImportCacheStats beforeSecond = ImportCache::Stats();
    REQUIRE(project.Load());
    CHECK(Since(beforeSecond).Hits == 1);
    CHECK(Since(beforeSecond).Stores == 0);

    // As the next run of the editor would find it
    ImportCache::ForgetMemory();

    const ImportCacheStats beforeNewProcess = ImportCache::Stats();
    REQUIRE(project.Load());
    CHECK(Since(beforeNewProcess).Hits == 1);
    CHECK(Since(beforeNewProcess).Stores == 0);
}

TEST_CASE("Changing the source compiles again", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;
    project.Write("mine.hlsl", kMinimalShader);
    REQUIRE(project.Load());

    project.Write("mine.hlsl", std::string(kMinimalShader) + " ");

    const ImportCacheStats before = ImportCache::Stats();
    CHECK(project.Load());
    CHECK(Since(before).Stores == 1);
    CHECK(Since(before).Hits == 0);
}

TEST_CASE("Changing an include in the shader's folder compiles again", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;
    project.Write("helper.hlsli", "#define HELPER_VALUE 1.0\n");
    project.Write("mine.hlsl", kHelperShader);
    REQUIRE(project.Load());

    SECTION("With the memory layer warm")
    {
        project.Write("helper.hlsli", "#define HELPER_VALUE 2.0\n");
    }

    SECTION("With the memory layer forgotten")
    {
        project.Write("helper.hlsli", "#define HELPER_VALUE 2.0\n");
        ImportCache::ForgetMemory();
    }

    const ImportCacheStats before = ImportCache::Stats();
    CHECK(project.Load());
    CHECK(Since(before).Stores == 1);
    CHECK(Since(before).Hits == 0);
}

TEST_CASE("A project file shadowing an engine header compiles again", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;
    project.Write("mine.hlsl", kEngineIncludeShader);
    REQUIRE(project.Load());

    project.Write("crimson/math.hlsli", "static const float PI = 3.0;\n");

    const ImportCacheStats before = ImportCache::Stats();
    CHECK(project.Load());
    CHECK(Since(before).Stores == 1);
    CHECK(Since(before).Hits == 0);
}

TEST_CASE("A changed engine header compiles again", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;
    project.Write("mine.hlsl", kEngineIncludeShader);

    // What an entry looks like that was made with another version of the header
    ImportCache cache(project.Cache(), project.Assets());
    const ImportDependency dependency{ ImportDependency::Origin::Engine, "shaders/crimson/math.hlsli", 12345 };
    cache.Store(kShaderCacheKind, ShaderCacheKey("mine.hlsl", kEngineIncludeShader), { &dependency, 1 }, WriteShaderBinary({}));

    const ImportCacheStats before = ImportCache::Stats();
    CHECK(project.Load());
    CHECK(Since(before).Stores == 1);
    CHECK(Since(before).Hits == 0);
}

TEST_CASE("An unknown annotation warns on a cache hit too", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;

    project.Write("mine.hlsl", R"(
cbuffer MaterialBlock : register(b0, space1)
{
    float Amount; // @bogus 1
};
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(Amount, 1.0, 1.0, 1.0); }
)");

    const LogCounter warnings(LogLevel::Warn, "can't be understood");
    const ImportCacheStats before = ImportCache::Stats();

    REQUIRE(project.Load());
    const int afterFirst = warnings.Count();
    CHECK(afterFirst > 0);

    REQUIRE(project.Load());
    CHECK(warnings.Count() == afterFirst * 2);
    CHECK(Since(before).Hits == 1);
}

TEST_CASE("A failed compile isn't cached", "[shader cache]")
{
    ImportCache::ForgetMemory();
    const ShaderProject project;
    project.Write("mine.hlsl", "this does not compile");

    const LogCounter errors(LogLevel::Error, "[Shader]");
    const ImportCacheStats before = ImportCache::Stats();

    CHECK_FALSE(project.Load());
    const int afterFirst = errors.Count();
    CHECK(afterFirst > 0);

    CHECK_FALSE(project.Load());
    CHECK(errors.Count() > afterFirst);

    CHECK(Since(before).Stores == 0);
    CHECK(Since(before).Hits == 0);
    CHECK((!fs::exists(project.Cache()) || fs::is_empty(project.Cache())));
}
