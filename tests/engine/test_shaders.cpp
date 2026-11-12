#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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

            // OpenGL links the depth version as a program of its own
            if (!binary->PixelDepth.empty())
            {
                std::vector<opengl::GlslTextureUnit> depthUnits;
                CHECK(binary->PixelDepth[0] == kSpirvMagic);
                CHECK(opengl::TranslateSpirvToGlsl(binary->PixelDepth, depthUnits));
            }
        }
    }

    CHECK(shaders >= 3);
}

TEST_CASE("A shader can have its own depth version, for the shadows and the outline", "[shaders]")
{
    SECTION("S_Lit has one, which cuts out like it")
    {
        const EmbeddedFile* lit = FindEmbeddedFile(GetEngineResources(), "shaders/S_Lit.hlsl");
        REQUIRE(lit);

        const auto binary = CompileHlslShader("S_Lit.hlsl", std::string(lit->Text()), {});
        REQUIRE(binary);
        CHECK_FALSE(binary->PixelDepth.empty());
    }

    SECTION("Without one there is none, and nothing goes wrong")
    {
        const auto binary = CompileHlslShader("minimal", std::string(kMinimalShader), {});
        REQUIRE(binary);
        CHECK(binary->PixelDepth.empty());
    }

    SECTION("What only it uses is part of the material")
    {
        const auto binary = CompileHlslShader("depth_only", std::string(kMinimalShader) + R"(
cbuffer MaterialBlock : register(b0, space2) { float Cutoff; };
Texture2D Mask : register(t0, space2);
void PSDepth(float4 position : SV_Position) { clip(Mask.Load(int3(0, 0, 0)).a - Cutoff); }
)", {});
        REQUIRE(binary);
        REQUIRE_FALSE(binary->PixelDepth.empty());

        const auto shaderInterface = ReflectShaderInterface(*binary);
        REQUIRE(shaderInterface);
        CHECK(shaderInterface->Textures.contains("Mask"));
        CHECK(shaderInterface->Properties.contains("Cutoff"));

        std::vector<opengl::GlslTextureUnit> units;
        CHECK(opengl::TranslateSpirvToGlsl(binary->PixelDepth, units));
    }
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

    CHECK(materials >= 3);
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

TEST_CASE("A compile lists the files it included from the shader's folder", "[shaders]")
{
    const TempFolder temp;
    const std::filesystem::path helper = temp.Write("helper.hlsli", "#include \"crimson/lighting.hlsli\"\n");

    std::vector<std::filesystem::path> included;

    SECTION("Not the engine's")
    {
        const std::string source = "#include \"helper.hlsli\"\n" + std::string(kMinimalShader);
        REQUIRE(CompileHlslShader("project.hlsl", source, temp.Path(), &included));
        CHECK(included == std::vector{ helper });
    }

    SECTION("Even when it fails, as fixing an included file can fix it")
    {
        const std::string source = "#include \"helper.hlsli\"\nthis does not compile";
        REQUIRE_FALSE(CompileHlslShader("project.hlsl", source, temp.Path(), &included));
        CHECK(included == std::vector{ helper });
    }
}

TEST_CASE("Annotations are read from the comments after declarations", "[shaders]")
{
    const auto annotations = ParseShaderAnnotations(R"(
// A comment @default 3 on its own line isn't about anything
cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color; // @default 1 0.5 0 1
    float Roughness; // The surface's roughness. @default 0.5 @range 0 1
    float Plain; // no tags in here
    float Broken; // @default soon @range 1
};

Texture2D NormalMap : register(t1, space2); // @default normal
)");

    REQUIRE(annotations.contains("Color"));
    CHECK(annotations.at("Color").Default == std::vector{ 1.0f, 0.5f, 0.0f, 1.0f });
    CHECK_FALSE(annotations.at("Color").Range);

    REQUIRE(annotations.contains("Roughness"));
    CHECK(annotations.at("Roughness").Default == std::vector{ 0.5f });
    CHECK(annotations.at("Roughness").Range == std::pair{ 0.0f, 1.0f });

    REQUIRE(annotations.contains("NormalMap"));
    CHECK(annotations.at("NormalMap").Texture == ShaderTextureDefault::Normal);

    CHECK_FALSE(annotations.contains("Plain"));

    REQUIRE(annotations.contains("Broken"));
    CHECK(annotations.at("Broken").Default.empty());
    CHECK_FALSE(annotations.at("Broken").Range);
}

TEST_CASE("Annotations reach the shader's interface", "[shaders]")
{
    const auto binary = CompileHlslShader("annotated", R"(
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Tint; // @default 1 1 1 1
    float Amount; // @default 0.25 @range 0 2
    float WrongSize; // @default 1 2
};

Texture2D Plain : register(t0, space2);
Texture2D Bumps : register(t1, space2); // @default normal

float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }

float4 PSMain() : SV_Target
{
    return Tint * Amount * WrongSize + Plain.Sample(LinearWrap, float2(0, 0)) + Bumps.Sample(LinearWrap, float2(0, 0));
}
)", {});
    REQUIRE(binary);

    const auto shader = ReflectShaderInterface(*binary);
    REQUIRE(shader);

    CHECK(shader->Properties.at("Tint").Default.size() == 4);
    CHECK(shader->Properties.at("Amount").Default == std::vector{ 0.25f });
    CHECK(shader->Properties.at("Amount").Range == std::pair{ 0.0f, 2.0f });
    CHECK(shader->Properties.at("WrongSize").Default.empty());
    CHECK(shader->Textures.at("Plain").Default == ShaderTextureDefault::White);
    CHECK(shader->Textures.at("Bumps").Default == ShaderTextureDefault::Normal);
}

TEST_CASE("A shader gets its camera from GetView, in either stage", "[shaders]")
{
    constexpr std::string_view kShader = R"(
#include "crimson/bindings.hlsli"

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(GetView().ViewProj, float4(position, 1.0));
}

float4 PSMain() : SV_Target
{
    return float4(GetView().CameraPosition.xyz, 1.0);
}
)";

    const auto binary = CompileHlslShader("view.hlsl", std::string(kShader), {});
    REQUIRE(binary);
    CHECK(ReflectShaderInterface(*binary));

    // Both stages have to end up with the same uniform for the draw's indices
    std::vector<opengl::GlslTextureUnit> units;
    const auto vertex = opengl::TranslateSpirvToGlsl(binary->Vertex, units);
    const auto pixel = opengl::TranslateSpirvToGlsl(binary->Pixel, units);
    REQUIRE(vertex);
    REQUIRE(pixel);
    CHECK(vertex->find(opengl::kDrawUniformName) != std::string::npos);
    CHECK(pixel->find(opengl::kDrawUniformName) != std::string::npos);
}

TEST_CASE("Nothing can be declared in space1, where the view used to be", "[shaders]")
{
    constexpr std::string_view kShader = R"(
cbuffer ViewBlock : register(b0, space1)
{
    float4x4 ViewProj;
};

float4 VSMain(float3 position : POSITION) : SV_Position { return mul(ViewProj, float4(position, 1.0)); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)";

    const auto binary = CompileHlslShader("old_view.hlsl", std::string(kShader), {});
    REQUIRE(binary);
    CHECK_FALSE(ReflectShaderInterface(*binary));
}

TEST_CASE("Shaders can use the math constants and the space helpers", "[shaders]")
{
    constexpr std::string_view kShader = R"(
#include "crimson/transforms.hlsli"

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Value : VALUE;
};

VertexOutput VSMain(float3 position : POSITION, float3 normal : NORMAL)
{
    const float3 world = ObjectToWorldPosition(position);

    VertexOutput output;
    output.ClipPosition = ObjectToClip(position) + WorldToClip(world) * EPSILON;
    output.Value = WorldToObjectPosition(world) + WorldToObjectDirection(ObjectToWorldDirection(normal))
        + ObjectToWorldNormal(normal) + ObjectWorldPosition();
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    return float4(input.Value * (PI + TAU + HALF_PI + INV_PI + E), 1.0);
}
)";

    const auto binary = CompileHlslShader("helpers.hlsl", std::string(kShader), {});
    REQUIRE(binary);
    CHECK(ReflectShaderInterface(*binary));

    std::vector<opengl::GlslTextureUnit> units;
    CHECK(opengl::TranslateSpirvToGlsl(binary->Vertex, units));
    CHECK(opengl::TranslateSpirvToGlsl(binary->Pixel, units));
}
