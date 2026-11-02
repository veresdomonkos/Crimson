#ifndef CRIMSON_SHADOWS_HLSLI
#define CRIMSON_SHADOWS_HLSLI

#include "crimson/bindings.hlsli"

// 1 is fully lit, 0 fully shadowed
float SampleShadow(uint shadowIndex, float3 worldPos, float3 normal)
{
    const GpuShadow shadow = Shadows[shadowIndex];

    const float4 clip = mul(shadow.ViewProj, float4(worldPos + normal * shadow.NormalBias, 1.0));
    if (clip.w <= 0.0)
        return 1.0;

    const float3 projected = clip.xyz / clip.w;
    const float2 tileUv = projected.xy * 0.5 + 0.5;

    if (projected.z > 1.0 || any(tileUv < 0.0) || any(tileUv > 1.0))
        return 1.0;

    // Half a texel of inset so filtering never reads the neighbouring tile
    const float2 uvMin = shadow.AtlasRect.xy + shadow.TexelSize * 0.5;
    const float2 uvMax = shadow.AtlasRect.xy + shadow.AtlasRect.zw - shadow.TexelSize * 0.5;
    const float2 uv = shadow.AtlasRect.xy + tileUv * shadow.AtlasRect.zw;
    const float depth = projected.z - shadow.DepthBias;

    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = clamp(uv + float2(x, y) * shadow.TexelSize, uvMin, uvMax);
            lit += ShadowAtlas.SampleCmpLevelZero(ShadowCompare, sampleUv, depth);
        }
    }

    return lit / 9.0;
}

// A point light's six tiles are ordered +X -X +Y -Y +Z -Z
uint GetPointShadowFace(float3 fromLight)
{
    const float3 absolute = abs(fromLight);

    if (absolute.x >= absolute.y && absolute.x >= absolute.z)
        return fromLight.x > 0.0 ? 0 : 1;

    if (absolute.y >= absolute.z)
        return fromLight.y > 0.0 ? 2 : 3;

    return fromLight.z > 0.0 ? 4 : 5;
}

#endif
