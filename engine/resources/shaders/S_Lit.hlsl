// The default lit shader: metallic-roughness PBR, with the same material slots as glTF.
// Colors are linear. Turn sRGB off on the textures in every slot but ColorMap and EmissiveMap.
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color; // @default 1 1 1 1
    float Metallic; // @default 0 @range 0 1
    float Roughness; // @default 0.5 @range 0 1
    float NormalStrength; // @default 1 @range 0 2
    float OcclusionStrength; // @default 1 @range 0 1
    float3 EmissiveColor; // @default 0 0 0
    float EmissiveStrength; // @default 1
    float AlphaCutoff; // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space1);
Texture2D NormalMap : register(t1, space1); // @default normal
Texture2D MetallicRoughnessMap : register(t2, space1); // green is roughness, blue is metallic
Texture2D OcclusionMap : register(t3, space1); // red
Texture2D EmissiveMap : register(t4, space1);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
};

VertexOutput VSMain(VertexInput input)
{
    const float3 worldPosition = ObjectToWorldPosition(input.Position);

    VertexOutput output;
    output.ClipPosition = WorldToClip(worldPosition);
    output.Normal = ObjectToWorldNormal(input.Normal);
    output.WorldPosition = worldPosition;
    output.UV = input.UV;
    output.Tangent = float4(ObjectToWorldDirection(input.Tangent.xyz), input.Tangent.w);
    return output;
}

// The tangent is the mesh's, along +U, with w the side the bitangent is on. A mesh without UVs has w = 0 and nothing to go by.
float3 ApplyNormalMap(float3 normal, float4 tangent, float2 uv)
{
    if (abs(tangent.w) < 0.5)
        return normal;

    float3 mapped = NormalMap.Sample(LinearWrap, uv).xyz * 2.0 - 1.0;
    mapped.xy *= NormalStrength;

    // Interpolation leaves it a little off the normal
    const float3 t = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
    const float3 b = cross(normal, t) * (tangent.w < 0.0 ? -1.0 : 1.0);

    // Green points up in the image, and V goes down it, as the first row of an image is at V = 0
    return normalize(t * mapped.x - b * mapped.y + normal * mapped.z);
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float4 metallicRoughness = MetallicRoughnessMap.Sample(LinearWrap, input.UV);
    const float occlusion = OcclusionMap.Sample(LinearWrap, input.UV).r;
    const float3 emissive = EmissiveColor * EmissiveStrength * EmissiveMap.Sample(LinearWrap, input.UV).rgb;

    const float3 geometricNormal = normalize(input.Normal);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = ApplyNormalMap(geometricNormal, input.Tangent, input.UV);
    surface.GeometricNormal = geometricNormal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic * metallicRoughness.b;
    surface.Roughness = Roughness * metallicRoughness.g;
    surface.Occlusion = lerp(1.0, occlusion, OcclusionStrength);

    const float3 color = ComputeLighting(surface) + emissive;

    // Last, as a discarded pixel stops at once, and the normal map's derivatives need the pixels around it. Only a
    // cutout material is given a cutoff above 0, which nothing is under.
    clip(base.a - AlphaCutoff);

    return float4(color, base.a);
}

// For the shadows and the selection outline, which only need depth: a cutout leaves the same holes there
void PSDepth(VertexOutput input)
{
    // Only a cutout material is given a cutoff above 0, and the others skip the texture
    if (AlphaCutoff > 0.0)
        clip((Color * ColorMap.Sample(LinearWrap, input.UV)).a - AlphaCutoff);
}
