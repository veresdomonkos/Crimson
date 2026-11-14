// Debug shader: a checkerboard laid out in UV space, 20 squares per UV unit, times Tiling.
// Every object uses the same scale, so squares can be compared across the scene: stretched squares mean
// distorted UVs, squares much bigger or smaller than on neighbouring surfaces mean the texture is spread
// thinner or packed denser there, and a pattern that jumps along a line marks a UV seam.
#include "crimson/transforms.hlsli"

static const float kCheckerScale = 20.0;

cbuffer MaterialBlock : register(b0, space1)
{
    float2 Tiling; // @default 1 1 @range 0.01 100
};

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
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.ClipPosition = WorldToClip(ObjectToWorldPosition(input.Position));
    output.UV = input.UV * Tiling;
    return output;
}

// The result is tone mapped like the rest of the scene, so the grays on screen are close to, not exactly, 0.2 and 0.8.
float4 PSMain(VertexOutput input) : SV_Target
{
    const float2 cell = floor(input.UV * kCheckerScale);
    const float checker = frac((cell.x + cell.y) * 0.5) * 2.0;
    return float4(lerp(0.2, 0.8, checker).xxx, 1.0);
}
