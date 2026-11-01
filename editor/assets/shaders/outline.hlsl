// Drawn over the whole screen, colors the pixels right outside the mask from outline_mask.hlsl
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 OutlineColor;
};

Texture2D OutlineMask : register(t0, space2);

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(float3 position : POSITION)
{
    VertexOutput output;
    output.ClipPosition = float4(position, 1.0);
    output.UV = position.xy * 0.5 + 0.5;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    static const int kThickness = 2;

    if (OutlineMask.Sample(PointClamp, input.UV).r > 0.5)
        return float4(0.0, 0.0, 0.0, 0.0);

    uint width, height;
    OutlineMask.GetDimensions(width, height);
    const float2 texelSize = float2(1.0 / width, 1.0 / height);

    for (int y = -kThickness; y <= kThickness; ++y)
    {
        for (int x = -kThickness; x <= kThickness; ++x)
        {
            if (OutlineMask.Sample(PointClamp, input.UV + float2(x, y) * texelSize).r > 0.5)
                return OutlineColor;
        }
    }

    return float4(0.0, 0.0, 0.0, 0.0);
}
