// S_LitMask's maps projected along the object's three axes instead of read at its UVs, for meshes without good UVs,
// such as rocks and blockouts. The projection is in the object's own space, so the texture moves and stretches with it.
// Turn sRGB off on NormalMap and MaskMap.
#include "crimson/lighting.hlsli"
#include "crimson/projections.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color;             // @default 1 1 1 1
    float Metallic;           // @default 1 @range 0 1
    float Roughness;          // @default 1 @range 0 1
    float NormalStrength;     // @default 1 @range 0 2
    float OcclusionStrength;  // @default 1 @range 0 1
    float3 EmissiveColor;     // @default 0 0 0
    float EmissiveStrength;   // @default 1
    float AlphaCutoff;        // @default 0.5 @range 0 1
    float ProjectionTiling;   // @default 1 @range 0.01 10
    float TriplanarSharpness; // @default 4 @range 1 16
};

Texture2D ColorMap : register(t0, space1);
Texture2D NormalMap : register(t1, space1);   // @default normal
Texture2D MaskMap : register(t2, space1);     // R: metallic, G: ao, B: reserved, A: roughness
Texture2D EmissiveMap : register(t3, space1);

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
};

VertexOutput VSMain(VertexInput input)
{
    const float3 worldPosition = ObjectToWorldPosition(input.Position);

    VertexOutput output;
    output.ClipPosition = WorldToClip(worldPosition);
    output.Normal = ObjectToWorldNormal(input.Normal);
    output.WorldPosition = worldPosition;
    return output;
}

float4 Project(Texture2D map, VertexOutput input)
{
    return SampleTriplanarObject(map, LinearWrap, input.WorldPosition, normalize(input.Normal), ProjectionTiling, TriplanarSharpness);
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 geometricNormal = normalize(input.Normal);

    const float4 base = Color * Project(ColorMap, input);
    const float4 mask = Project(MaskMap, input);
    const float3 emissive = EmissiveColor * EmissiveStrength * Project(EmissiveMap, input).rgb;

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = SampleTriplanarNormalObject(NormalMap, LinearWrap, input.WorldPosition, geometricNormal, ProjectionTiling, TriplanarSharpness, NormalStrength);
    surface.GeometricNormal = geometricNormal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic * mask.r;
    surface.Roughness = Roughness * mask.a;
    surface.Occlusion = lerp(1.0, mask.g, OcclusionStrength);

    const float3 color = ComputeLighting(surface) + emissive;

    // Last, as a discarded pixel stops at once, and the projections' derivatives need the pixels around it
    clip(base.a - AlphaCutoff);

    return float4(color, base.a);
}

// For the shadows and the selection outline, which only need depth: a cutout leaves the same holes there
void PSDepth(VertexOutput input)
{
    // Only a cutout material is given a cutoff above 0, and the others skip the texture
    if (AlphaCutoff > 0.0)
        clip((Color * Project(ColorMap, input)).a - AlphaCutoff);
}
