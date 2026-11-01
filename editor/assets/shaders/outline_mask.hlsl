// Marks where the selected meshes are, for outline.hlsl
#include "crimson/bindings.hlsli"

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(ViewProj, mul(GetObjectData().Transform, float4(position, 1.0)));
}

float4 PSMain() : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
