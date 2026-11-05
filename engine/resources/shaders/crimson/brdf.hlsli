#ifndef CRIMSON_BRDF_HLSLI
#define CRIMSON_BRDF_HLSLI

#include "crimson/math.hlsli"

// How much of the surface's microfacets face the half vector
float DistributionGGX(float NdotH, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float denominator = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denominator * denominator, EPSILON);
}

// Smith's height-correlated masking and shadowing, already divided by 4 * NdotL * NdotV
float VisibilitySmithGGX(float NdotL, float NdotV, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0 - alpha2) + alpha2);
    const float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0 - alpha2) + alpha2);
    return 0.5 / max(lambdaV + lambdaL, 0.000001);
}

float3 FresnelSchlick(float VdotH, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - VdotH), 5.0);
}

// Brian Karis's fit of the specular light a uniform environment reflects
float3 EnvironmentBRDF(float3 f0, float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

#endif
