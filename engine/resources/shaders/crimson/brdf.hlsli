#ifndef CRIMSON_BRDF_HLSLI
#define CRIMSON_BRDF_HLSLI

#include "crimson/math.hlsli"

// How much of the surface's microfacets face the half vector.
float DistributionGGX(float NdotH, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float denominator = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denominator * denominator, EPSILON);
}

// Smith's height-correlated masking and shadowing, already divided by 4 * NdotL * NdotV.
float VisibilitySmithGGX(float NdotL, float NdotV, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0 - alpha2) + alpha2);
    const float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0 - alpha2) + alpha2);
    return 0.5 / max(lambdaV + lambdaL, 0.000001);
}

float3 FresnelSchlick(float VdotH, float3 f0)
{
    return f0 + (1.0 - f0) * Pow5(saturate(1.0 - VdotH));
}

// Brian Karis's fit of the specular light a uniform environment reflects.
float3 EnvironmentBRDF(float3 f0, float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// The paper on the Frostbite Burley implementation (https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf)
// talks about the Disney model not being energy conserving, so the output is normalized.
// Rough surfaces flatten and get brighter at grazing angles, smooth ones darken there. roughness is the material's, not squared.
float DiffuseBurley(float NdotL, float NdotV, float LdotH, float roughness)
{
    const float energyBias = 0.5 * roughness;
    const float energyFactor = lerp(1.0, 1.0 / 1.51, roughness);
    const float f90 = energyBias + 2.0 * LdotH * LdotH * roughness;

    const float lightScatter = 1.0 + (f90 - 1.0) * Pow5(1.0 - NdotL);
    const float viewScatter = 1.0 + (f90 - 1.0) * Pow5(1.0 - NdotV);
    return lightScatter * viewScatter * energyFactor;
}

// The Callisto BRDF (https://advances.realtimerendering.com/s2023/SIGGRAPH2023-Advances-The-Rendering-of-The-Callisto-Protocol-JimenezPetersen.pdf).
// Lambert + GGX, with controls added on top of both.
struct CallistoParams
{
    float3 DiffuseFresnel;              // brightens or darkens the diffuse at grazing angles: intensity times tint, 1 leaves it as it is
    float DiffuseFresnelFalloff;        // 0 to 1, how fast towards grazing it builds up. 0.75
    float DiffuseFresnelTangentFalloff; // 0 to 1, how much it stays where the half vector leans away from the normal. 0.75
    float3 Retroreflection;             // flattens or enhances the shape when lit from the camera's side: intensity times tint, 1 leaves it
    float RetroreflectionFalloff;       // 0.75
    float RetroreflectionTangentFalloff; // 0.75
    float SmoothTerminator;             // -1 to 1: above 0 the light fades out sooner before the terminator, below 0 later. 0
    float SmoothTerminatorLength;       // 0 to 1, how far from the terminator that reaches. 0.5
    float SpecularFresnelFalloff;       // 0 to 1: 0.5 is Schlick's Fresnel, 0 removes it, 1 reflects everything like a grazing angle
    float DualSpecularRoughnessScale;   // the second highlight's roughness, as a multiple of the first's. 2
    float DualSpecularOpacity;          // how much of the second highlight is used instead of the first: 0 leaves only the first
};

// Callisto's diffuse as a scale on Lambert's albedo / PI, like DiffuseBurley: c1, the diffuse Fresnel times the retroreflection.
// Colored, as both are tinted. LdotH is cos θd and NdotH is cos θh. The smooth terminator is separate, as it scales the specular too.
float3 DiffuseCallisto(float LdotH, float NdotH, CallistoParams params)
{
    // Each is h(θ, n, φ, m) = (1 - cos θ)^5r(n) (cos φ)^5r(m): where along θ it shows, and how much it stays as φ grows.
    // r(x) = 2 (1 - x) turns a falloff from 0 to 1 into a multiple of Schlick's exponent of 5, so 5 r(x) = 10 (1 - x), and 0.5 is Schlick's own.
    const float fresnel = SafePow(1.0 - saturate(LdotH), 10.0 * (1.0 - params.DiffuseFresnelFalloff)) * SafePow(saturate(NdotH), 10.0 * (1.0 - params.DiffuseFresnelTangentFalloff));
    const float retroreflection = SafePow(1.0 - saturate(NdotH), 10.0 * (1.0 - params.RetroreflectionFalloff)) * SafePow(saturate(LdotH), 10.0 * (1.0 - params.RetroreflectionTangentFalloff));

    return lerp(float3(1.0, 1.0, 1.0), params.DiffuseFresnel, fresnel) * lerp(float3(1.0, 1.0, 1.0), params.Retroreflection, retroreflection);
}

// c2: the smooth terminator, which multiplies both diffuse and specular.
// Its mask is 0 where the retroreflection (θd near 0) and the Fresnel (θh near 0) show, so it leaves those alone.
float CallistoTerminator(float NdotL, float LdotH, float NdotH, CallistoParams params)
{
    // The slides take the angles themselves here, not their cosines. Past 1 radian the base goes negative, which a cube handles and pow doesn't.
    const float d = 1.0 - acos(saturate(LdotH));
    const float h = 1.0 - acos(saturate(NdotH));
    const float mask = (1.0 - d * d * d) * (1.0 - h * h * h);

    // smoothstep(0, mask * length, NdotL), kept from dividing by zero where the mask or the length is 0
    const float t = saturate(NdotL / max(mask * params.SmoothTerminatorLength, 0.0001));
    const float smoothed = t * t * (3.0 - 2.0 * t);

    return lerp(1.0, smoothed, mask * params.SmoothTerminator);
}

// Schlick's Fresnel with the exponent as a control, and able to turn the Fresnel off: 0.5 is FresnelSchlick
float3 FresnelSchlickFalloff(float VdotH, float3 f0, float falloff)
{
    // r(x) = 2 (1 - x), as for the diffuse falloffs: the exponent is 5 r and the Fresnel's strength t(2 - r), which 0 turns off
    const float r = 2.0 * (1.0 - falloff);
    return f0 + saturate(2.0 - r) * (1.0 - f0) * SafePow(1.0 - saturate(VdotH), 5.0 * r);
}

#endif
