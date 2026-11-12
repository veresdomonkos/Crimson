// Drawn over the whole screen, turns the linear HDR scene into sRGB encoded colors for the display
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float Exposure;
    float ToneMapper; // SceneRenderer's ToneMapper enum
};

Texture2D SceneColor : register(t0, space1);

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

// Krzysztof Narkowicz's fit of the ACES filmic curve
float3 ToneMapAces(float3 color)
{
    return (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
}

float3 EncodeSrgb(float3 linearColor)
{
    const float3 low = linearColor * 12.92;
    const float3 high = 1.055 * pow(max(linearColor, 0.0031308), 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(linearColor, 0.0031308));
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 scene = SceneColor.Sample(PointClamp, input.UV).rgb * Exposure;
    const float3 mapped = ToneMapper < 0.5 ? ToneMapAces(scene) : scene;

    return float4(EncodeSrgb(saturate(mapped)), 1.0);
}
