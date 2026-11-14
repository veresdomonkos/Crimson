// Color * ColorMap, with no lighting. Alpha blends when the material is Transparent, and cuts out when it is Cutout.
#include "crimson/transforms.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color;      // @default 1 1 1 1
    // A float2 has to sit on an 8-byte boundary for OpenGL, so Tiling goes before the lone float after it
    float2 Tiling;     // @default 1 1 @range 0.01 100
    float AlphaCutoff; // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space1);

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

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);

    // Last, as a discarded pixel stops at once, and the derivatives need the pixels around it.
    // Must match the clip in PSDepth, so the shadows and the selection outline get the same holes.
    clip(base.a - AlphaCutoff);

    return base;
}

// For the shadows and the selection outline, which only need depth: a cutout leaves the same holes there
void PSDepth(VertexOutput input)
{
    if (AlphaCutoff > 0.0)
        clip((Color * ColorMap.Sample(LinearWrap, input.UV)).a - AlphaCutoff);
}
