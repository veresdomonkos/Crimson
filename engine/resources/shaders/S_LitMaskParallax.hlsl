// S_LitMask with parallax occlusion mapping: the view ray is marched down through HeightMap (white is the top), so the surface looks sunken where it is dark.
// The shadows and the selection outline use the flat surface. Turn sRGB off on NormalMap, MaskMap and HeightMap.
#include "crimson/normals.hlsli"
#include "crimson/lighting.hlsli"
#include "crimson/parallax.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color;                 // @default 1 1 1 1
    float Metallic;               // @default 1 @range 0 1
    float Roughness;              // @default 1 @range 0 1
    float NormalStrength;         // @default 1 @range 0 2
    float OcclusionStrength;      // @default 1 @range 0 1
    float3 EmissiveColor;         // @default 0 0 0
    float EmissiveStrength;       // @default 1
    // A float2 has to sit on an 8-byte boundary for OpenGL, so Tiling goes before the lone float after it
    float2 Tiling;                // @default 1 1 @range 0.01 100
    float AlphaCutoff;            // @default 0.5 @range 0 1
    float ParallaxHeightStrength; // @default 0.025 @range 0 0.1
    float ParallaxSteps;          // @default 8 @range 1 32
};

Texture2D ColorMap : register(t0, space1);
Texture2D NormalMap : register(t1, space1);   // @default normal
Texture2D MaskMap : register(t2, space1);     // R: metallic, G: ao, B: reserved, A: roughness
Texture2D EmissiveMap : register(t3, space1);
Texture2D HeightMap : register(t4, space1);   // R: height, white = top

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
    output.UV = input.UV * Tiling;
    output.Tangent = ObjectToWorldTangent(input.Tangent);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    // The flat UV's derivatives come first, as the parallax UV jumps between steps, which would pick the wrong mips
    const float2 dx = ddx(input.UV);
    const float2 dy = ddy(input.UV);

    const float3 geometricNormal = normalize(input.Normal);
    const float3 viewTS = WorldToTangentDirection(GetViewDirection(input.WorldPosition), TangentFrame(geometricNormal, input.Tangent));
    const float2 uv = ParallaxOcclusion(HeightMap, input.UV, viewTS, ParallaxHeightStrength, ParallaxSteps);

    const float4 base = Color * ColorMap.SampleGrad(LinearWrap, uv, dx, dy);
    const float4 mask = MaskMap.SampleGrad(LinearWrap, uv, dx, dy);
    const float3 emissive = EmissiveColor * EmissiveStrength * EmissiveMap.SampleGrad(LinearWrap, uv, dx, dy).rgb;
    const float3 tangentNormal = ApplyNormalStrength(UnpackNormalXYZ(NormalMap.SampleGrad(LinearWrap, uv, dx, dy)), NormalStrength);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = ApplyNormalMap(geometricNormal, input.Tangent, tangentNormal);
    surface.GeometricNormal = geometricNormal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic * mask.r;
    surface.Roughness = Roughness * mask.a;
    surface.Occlusion = lerp(1.0, mask.g, OcclusionStrength);

    const float3 color = ComputeLighting(surface) + emissive;

    // Last, as a discarded pixel stops at once, and the derivatives need the pixels around it.
    // PSDepth cuts at the flat UV, so the holes in the shadows and the outline can be slightly off from these.
    clip(base.a - AlphaCutoff);

    return float4(color, base.a);
}

// For the shadows and the selection outline, which only need depth: a cutout leaves the same holes there.
// No parallax here: the shadow pass's view isn't the camera's, so the flat surface is used.
void PSDepth(VertexOutput input)
{
    // Only a cutout material is given a cutoff above 0, and the others skip the texture
    if (AlphaCutoff > 0.0)
        clip((Color * ColorMap.Sample(LinearWrap, input.UV)).a - AlphaCutoff);
}
