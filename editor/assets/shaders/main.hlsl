#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color;
};

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
};

VertexOutput VSMain(VertexInput input)
{
    const ObjectData object = GetObjectData();
    const float4 worldPosition = mul(object.Transform, float4(input.Position, 1.0));

    VertexOutput output;
    output.ClipPosition = mul(ViewProj, worldPosition);
    output.Normal = mul((float3x3)object.NormalMatrix, input.Normal);
    output.WorldPosition = worldPosition.xyz;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 lighting = ComputeLighting(input.WorldPosition, normalize(input.Normal));
    return float4(Color.rgb * lighting, Color.a);
}
