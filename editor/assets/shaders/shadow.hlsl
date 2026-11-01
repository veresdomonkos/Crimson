#include "crimson/bindings.hlsli"

float4 VSMain(float3 position : POSITION) : SV_Position
{
    return mul(ViewProj, mul(GetObjectData().Transform, float4(position, 1.0)));
}

void PSMain()
{
}
