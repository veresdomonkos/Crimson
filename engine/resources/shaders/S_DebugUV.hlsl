// Debug shader: a checkerboard laid out in UV space, checkerScale squares along each of U and V.
// Every object uses the same scale, so squares can be compared across the scene: stretched squares mean
// distorted UVs, squares much bigger or smaller than on neighbouring surfaces mean the texture is spread
// thinner or packed denser there, and a pattern that jumps along a line marks a UV seam.
#include "crimson/transforms.hlsli"

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
    output.UV = input.UV;
    return output;
}

// TODO: No tonemapping
float4 PSMain(VertexOutput input) : SV_Target
{
    const float checkerScale = 20; // maybe make this an adjustable global?
    
    const float2 cell = floor(input.UV * checkerScale);
    const float checker = frac((cell.x + cell.y) * 0.5) * 2.0;
    return float4(lerp(0.2, 0.8, checker).xxx, 1.0);
}
