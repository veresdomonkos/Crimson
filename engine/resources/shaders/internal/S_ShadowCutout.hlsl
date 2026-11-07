// S_Shadow for a cutout material: what a material's AlphaCutoff cuts away doesn't cast a shadow either.
// The asset manager gives it the material's ColorMap, Color and AlphaCutoff.
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color; // @default 1 1 1 1
    float AlphaCutoff; // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space2);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.ClipPosition = mul(ViewProj, mul(GetObjectData().Transform, float4(input.Position, 1.0)));
    output.UV = input.UV;
    return output;
}

void PSMain(VertexOutput input)
{
    clip((Color * ColorMap.Sample(LinearWrap, input.UV)).a - AlphaCutoff);
}
