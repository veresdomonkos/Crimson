#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color;
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
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const ObjectData object = GetObjectData();
    const float4 worldPosition = mul(object.Transform, float4(input.Position, 1.0));

    VertexOutput output;
    output.ClipPosition = mul(ViewProj, worldPosition);
    output.Normal = mul((float3x3)object.NormalMatrix, input.Normal);
    output.WorldPosition = worldPosition.xyz;
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float3 lighting = ComputeLighting(input.WorldPosition, normalize(input.Normal));
    return float4(base.rgb * lighting, base.a);
}
