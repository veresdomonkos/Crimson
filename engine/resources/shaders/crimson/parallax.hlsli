#ifndef CRIMSON_PARALLAX_HLSLI
#define CRIMSON_PARALLAX_HLSLI

#include "crimson/bindings.hlsli"

// Parallax occlusion mapping: marches the view ray down through the height map (white = top) and returns the UV where it hits.
float2 ParallaxOcclusion(Texture2D heightMap, float2 uv, float3 viewTS, float heightStrength, float stepCount)
{
    // Derivatives of the original UV, so the loop's samples pick the same mip as the surface.
    // Before any branch: derivatives aren't reliable inside one.
    const float2 dx = ddx(uv);
    const float2 dy = ddy(uv);

    // How far, in pixels, the effect moves the texture at most.
    const float2 offset = viewTS.xy / max(viewTS.z, 0.1) * heightStrength;
    const float uvPerPixel = sqrt(max(dot(dx, dx), dot(dy, dy)));
    const float offsetPixels = length(offset) / max(uvPerPixel, 1e-8);

    // Above 1: fewer steps until the movement gets large. Below 1: more steps sooner.
    const float stepCurve = 2.0;
    const float steps = max(ceil(stepCount * pow(saturate(offsetPixels / stepCount), stepCurve)), 1.0);

    const float layerDepth = 1.0 / steps;
    const float2 delta = offset / steps;

    float2 current = uv;
    float currentDepth = 0.0;
    float mapDepth = 1.0 - heightMap.SampleGrad(LinearWrap, current, dx, dy).r;

    [loop]
    for (int i = 0; i < 64 && currentDepth < mapDepth; ++i)
    {
        current -= delta;
        currentDepth += layerDepth;
        mapDepth = 1.0 - heightMap.SampleGrad(LinearWrap, current, dx, dy).r;
    }

    // Interpolate between the last two steps for a smooth result instead of visible layers
    const float2 previous = current + delta;
    const float after = mapDepth - currentDepth;
    const float before = (1.0 - heightMap.SampleGrad(LinearWrap, previous, dx, dy).r) - (currentDepth - layerDepth);
    const float weight = after / (after - before);

    return lerp(current, previous, saturate(weight));
}

#endif
