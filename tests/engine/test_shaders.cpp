#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <crimson/core/engine_resources.hpp>
#include <crimson/renderer/shader_compiler.hpp>

#include "platform/opengl/spirv_to_glsl.hpp"
#include "shader/shader_reflection.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::TempFolder;

static constexpr std::string_view kMinimalShader = R"(
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)";

static constexpr uint32_t kSpirvMagic = 0x07230203;

// Everything a backend does with a shader that doesn't need a GPU, so a shader that only breaks on one backend is caught too
TEST_CASE("Every engine shader compiles, reflects and translates to GLSL", "[shaders]")
{
    int shaders = 0;

    for (const EmbeddedFile& file : GetEngineResources())
    {
        if (!file.Path.ends_with(".hlsl"))
            continue;

        ++shaders;
        DYNAMIC_SECTION(file.Path)
        {
            const auto binary = CompileHlslShader(std::string(file.Path), std::string(file.Text()), {});
            REQUIRE(binary);
            REQUIRE_FALSE(binary->Vertex.empty());
            REQUIRE_FALSE(binary->Pixel.empty());
            CHECK(binary->Vertex[0] == kSpirvMagic);
            CHECK(binary->Pixel[0] == kSpirvMagic);

            CHECK(ReflectShaderInterface(*binary));

            std::vector<opengl::GlslTextureUnit> units;
            CHECK(opengl::TranslateSpirvToGlsl(binary->Vertex, units));
            CHECK(opengl::TranslateSpirvToGlsl(binary->Pixel, units));
        }
    }

    CHECK(shaders >= 4);
}

TEST_CASE("Every engine material names an engine shader", "[shaders]")
{
    const std::span<const EmbeddedFile> resources = GetEngineResources();
    int materials = 0;

    for (const EmbeddedFile& file : resources)
    {
        if (!file.Path.ends_with(".mat"))
            continue;

        ++materials;
        CAPTURE(file.Path);

        const nlohmann::json json = nlohmann::json::parse(file.Text());
        const std::string shader = json.at("shader").get<std::string>();

        REQUIRE(shader.starts_with("engine/"));
        CHECK(FindEmbeddedFile(resources, std::string_view(shader).substr(7)) != nullptr);
    }

    CHECK(materials >= 4);
}

TEST_CASE("Embedded files can be found by their path", "[shaders]")
{
    CHECK(FindEmbeddedFile(GetEngineResources(), "shaders/crimson/bindings.hlsli") != nullptr);
    CHECK(FindEmbeddedFile(GetEngineResources(), "no/such/file") == nullptr);
}

TEST_CASE("Broken shaders report failure instead of crashing", "[shaders]")
{
    CHECK_FALSE(CompileHlslShader("broken", "this does not compile", {}));
    CHECK_FALSE(CompileHlslShader("empty", "", {}));
    CHECK_FALSE(CompileHlslShader("missing include", "#include \"no/such/file.hlsli\"\n" + std::string(kMinimalShader), {}));
}

TEST_CASE("Shader includes look in the shader's folder, then in the engine's", "[shaders]")
{
    const TempFolder temp;

    SECTION("The engine's are found from any folder")
    {
        const std::string source = "#include \"crimson/lighting.hlsli\"\n" + std::string(kMinimalShader);
        CHECK(CompileHlslShader("project.hlsl", source, temp.Path()));
    }

    SECTION("And without any folder")
    {
        const std::string source = "#include \"crimson/lighting.hlsli\"\n" + std::string(kMinimalShader);
        CHECK(CompileHlslShader("embedded.hlsl", source, {}));
    }

    SECTION("The shader's own folder is used")
    {
        temp.Write("helper.hlsli", "#define HELPER_VALUE 1.0\n");
        const std::string source = "#include \"helper.hlsli\"\n" + std::string(kMinimalShader);
        CHECK(CompileHlslShader("project.hlsl", source, temp.Path()));
    }

    SECTION("And wins over the engine's")
    {
        temp.Write("crimson/bindings.hlsli", "this is not the engine's file");
        const std::string source = "#include \"crimson/bindings.hlsli\"\n" + std::string(kMinimalShader);
        CHECK_FALSE(CompileHlslShader("project.hlsl", source, temp.Path()));
    }
}
