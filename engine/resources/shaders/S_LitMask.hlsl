// Lit shader with unity hdrp style mask map with roughness (not smoothness) stored in the A channel.
#include "crimson/normals.hlsli"
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color;            // @default 1 1 1 1
    float Metallic;          // @default 1 @range 0 1
    float Roughness;         // @default 1 @range 0 1
    float NormalStrength;    // @default 1 @range 0 2
    float OcclusionStrength; // @default 1 @range 0 1
    float3 EmissiveColor;    // @default 0 0 0
    float EmissiveStrength;  // @default 1
    float AlphaCutoff;       // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space1);
Texture2D NormalMap : register(t1, space1);   // @default normal
Texture2D MaskMap : register(t2, space1);     // R: metallic, G: ao, B: reserved, A: roughness
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
    output.Tangent = ObjectToWorldTangent(input.Tangent);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float4 mask = MaskMap.Sample(LinearWrap, input.UV);
    const float3 emissive = EmissiveColor * EmissiveStrength * EmissiveMap.Sample(LinearWrap, input.UV).rgb;

    const float3 geometricNormal = normalize(input.Normal);
    const float3 tangentNormal = ApplyNormalStrength(UnpackNormalXYZ(NormalMap.Sample(LinearWrap, input.UV)), NormalStrength);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = ApplyNormalMap(geometricNormal, input.Tangent, tangentNormal);
    surface.GeometricNormal = geometricNormal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic * mask.r;
    surface.Occlusion = lerp(1.0, mask.g, OcclusionStrength);
    surface.Roughness = Roughness * mask.a;

    const float3 color = ComputeLighting(surface) + emissive;
    
    // Discard pixels whose alpha is below the cutoff.
    // Must match the clip in PSDepth, so the shadows and the selection outline get the same holes.
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
