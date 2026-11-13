#include "crimson/asset_manager/importers/shader_importer.hpp"

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"
#include "crimson/renderer/shader_compiler.hpp"

namespace crimson
{
    // What a new shader starts as: lit like the default one, with fewer slots, to have something to change
    static constexpr std::string_view kNewShaderSource = R"(// VSMain and PSMain are the entry points. The material's properties go in the one cbuffer in space1,
// and its textures in space1 too. A comment like "@default 0.5 @range 0 1" sets how a property starts.
// Time and DeltaTime, in seconds, come from crimson/bindings.hlsli. crimson/lighting.hlsli also brings
// crimson/math.hlsli (PI, TAU, EPSILON, ...) and crimson/transforms.hlsli (ObjectToWorldPosition, WorldToClip, ...).
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color; // @default 1 1 1 1
    float Metallic; // @default 0 @range 0 1
    float Roughness; // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space1);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const float3 worldPosition = ObjectToWorldPosition(input.Position);

    VertexOutput output;
    output.ClipPosition = WorldToClip(worldPosition);
    output.Normal = ObjectToWorldNormal(input.Normal);
    output.WorldPosition = worldPosition;
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float3 normal = normalize(input.Normal);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = normal;
    surface.GeometricNormal = normal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic;
    surface.Roughness = Roughness;
    surface.Occlusion = 1.0;

    return float4(ComputeLighting(surface), base.a);
}
)";

    std::span<const std::string_view> ShaderImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".hlsl" };
        return exts;
    }

    nlohmann::json ShaderImporter::DefaultSettings() const
    {
        return nlohmann::json::object();
    }

    std::optional<std::string> ShaderImporter::NewFile(const AssetManager&) const
    {
        return std::string(kNewShaderSource);
    }

    std::optional<ShaderHandle> ShaderImporter::Load(const AssetSource& source, ImportContext& ctx)
    {
        const auto text = source.Read();
        if (!text)
            return {};

        const auto binary = CompileHlslShader(source.Info.Path, *text, source.File.parent_path(), &ctx.Dependencies);
        if (!binary)
        {
            LOG_ERROR("[ShaderImporter] Compilation failed for '{}'", source.Info.Path);
            return {};
        }

        const ShaderHandle handle = ctx.Gpu.CreateShader(*binary);
        if (!handle)
        {
            LOG_ERROR("[ShaderImporter] CreateShader failed for '{}'", source.Info.Path);
            return {};
        }

        return handle;
    }

    void ShaderImporter::Unload(ShaderHandle& shader, GpuResourceManager& gpu)
    {
        gpu.DestroyShader(shader);
    }
}
