#ifndef CRIMSON_LIGHTING_HLSLI
#define CRIMSON_LIGHTING_HLSLI

#include "crimson/bindings.hlsli"
#include "crimson/brdf.hlsli"
#include "crimson/shadows.hlsli"
#include "crimson/transforms.hlsli"

static const uint LIGHT_DIRECTIONAL = 0;
static const uint LIGHT_POINT = 1;
static const uint LIGHT_SPOT = 2;

static const float MIN_ROUGHNESS = 0.045;
static const float DIELECTRIC_F0 = 0.04;

// What the lighting needs to know about a point on a surface. Colors are linear.
struct Surface
{
    float3 WorldPosition;
    float3 Normal;          // with the normal map applied
    float3 GeometricNormal; // of the mesh itself, for what depends on where the surface really is
    float3 Albedo;
    float Metallic;
    float Roughness;
    float Occlusion;
};

// One of the scene's lights, as it reaches a point on a surface
struct LightSample
{
    float3 Irradiance; // the light's color and intensity after attenuation, its cone and its shadow, times NdotL
    float NdotL;
    float NdotH;
    float VdotH;       // V and L are mirrored around H, so this is also LdotH, the cos θd of the BRDFs
};

// False when the light doesn't reach the surface: it is behind it, out of its range or cone, or the surface faces away
bool SampleLight(uint index, Surface surface, float3 view, out LightSample reached)
{
    reached = (LightSample)0;
    const GpuLight light = Lights[index];

    float3 toLight;
    float attenuation = 1.0;

    if (light.Type == LIGHT_DIRECTIONAL)
    {
        toLight = normalize(-light.Direction);
    }
    else
    {
        const float3 offset = light.Position - surface.WorldPosition;
        const float distance = length(offset);
        toLight = offset / max(distance, 0.0001);

        // Inverse square, going smoothly to zero at the light's range
        const float rangeRatio = distance / max(light.Range, 0.0001);
        const float window = saturate(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio);
        attenuation = window * window / max(distance * distance, 0.01);

        if (light.Type == LIGHT_SPOT)
        {
            const float cosAngle = dot(-toLight, normalize(light.Direction));
            attenuation *= saturate((cosAngle - light.SpotOuterCos) / max(light.SpotInnerCos - light.SpotOuterCos, 0.0001));
        }
    }

    const float NdotL = dot(surface.Normal, toLight);
    if (NdotL <= 0.0 || attenuation <= 0.0)
        return false;

    float shadow = 1.0;
    if (light.ShadowIndex >= 0)
    {
        const uint face = light.Type == LIGHT_POINT ? GetPointShadowFace(-toLight) : 0;
        shadow = SampleShadow(light.ShadowIndex + face, surface.WorldPosition, surface.GeometricNormal);
    }

    const float3 halfVector = normalize(toLight + view);

    reached.Irradiance = light.Color * light.Intensity * attenuation * shadow * NdotL;
    reached.NdotL = NdotL;
    reached.NdotH = saturate(dot(surface.Normal, halfVector));
    reached.VdotH = saturate(dot(view, halfVector));
    return true;
}

// The light leaving the surface towards the camera, in linear light, with Burley diffuse and GGX specular.
float3 ComputeLightingBurley(Surface surface)
{
    const float3 view = normalize(GetView().CameraPosition.xyz - surface.WorldPosition);
    const float NdotV = max(dot(surface.Normal, view), 0.0001);
    const float roughness = clamp(surface.Roughness, MIN_ROUGHNESS, 1.0);
    const float alpha = roughness * roughness;

    // Metals have no diffuse light, and tint their reflections instead
    const float3 f0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), surface.Albedo, surface.Metallic);
    const float3 diffuseColor = surface.Albedo * (1.0 - surface.Metallic);

    // Without an environment map, ambient light counts as a uniform environment. What it reflects can't also be diffuse.
    const float3 ambientSpecular = EnvironmentBRDF(f0, roughness, NdotV);
    float3 result = (diffuseColor * (1.0 - ambientSpecular) + ambientSpecular) * AmbientColor.rgb * surface.Occlusion;

    for (uint i = 0; i < LightCount; ++i)
    {
        LightSample light;
        if (!SampleLight(i, surface, view, light))
            continue;

        const float3 specular = FresnelSchlick(light.VdotH, f0) * DistributionGGX(light.NdotH, alpha) * VisibilitySmithGGX(light.NdotL, NdotV, alpha);

        // Burley's own grazing term stands in for what the specular takes away, as in Frostbite
        const float3 diffuse = DiffuseBurley(light.NdotL, NdotV, light.VdotH, roughness) * diffuseColor / PI;

        result += (diffuse + specular) * light.Irradiance;
    }

    return result;
}

// The light leaving the surface towards the camera, in linear light, with the Callisto BRDF:
// Lambert + GGX at DefaultCallistoParams(), and the material's controls on top.
float3 ComputeLightingCallisto(Surface surface, CallistoParams params)
{
    const float3 view = normalize(GetView().CameraPosition.xyz - surface.WorldPosition);
    const float NdotV = max(dot(surface.Normal, view), 0.0001);
    const float roughness = clamp(surface.Roughness, MIN_ROUGHNESS, 1.0);
    const float alpha = roughness * roughness;

    // Metals have no diffuse light, and tint their reflections instead
    const float3 f0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), surface.Albedo, surface.Metallic);
    const float3 diffuseColor = surface.Albedo * (1.0 - surface.Metallic);

    // Without an environment map, ambient light counts as a uniform environment. What it reflects can't also be diffuse.
    // It doesn't follow the Callisto controls, which only change how the scene's lights shade the surface.
    const float3 ambientSpecular = EnvironmentBRDF(f0, roughness, NdotV);
    float3 result = (diffuseColor * (1.0 - ambientSpecular) + ambientSpecular) * AmbientColor.rgb * surface.Occlusion;

    for (uint i = 0; i < LightCount; ++i)
    {
        LightSample light;
        if (!SampleLight(i, surface, view, light))
            continue;

        float highlight = DistributionGGX(light.NdotH, alpha) * VisibilitySmithGGX(light.NdotL, NdotV, alpha);

        // A second, rougher (or smoother) highlight blended in, for what one can't match
        if (params.DualSpecularOpacity > 0.0)
        {
            const float dualRoughness = clamp(roughness * params.DualSpecularRoughnessScale, MIN_ROUGHNESS, 1.0);
            const float dualAlpha = dualRoughness * dualRoughness;
            const float dualHighlight = DistributionGGX(light.NdotH, dualAlpha) * VisibilitySmithGGX(light.NdotL, NdotV, dualAlpha);
            highlight = lerp(highlight, dualHighlight, saturate(params.DualSpecularOpacity));
        }

        const float terminator = CallistoTerminator(light.NdotL, light.VdotH, light.NdotH, params);
        const float3 specular = FresnelSchlickFalloff(light.VdotH, f0, params.SpecularFresnelFalloff) * highlight * terminator;

        // As in the talk, without taking out what the specular reflects: the diffuse Fresnel controls are fitted to that
        const float3 diffuse = DiffuseCallisto(light.VdotH, light.NdotH, params) * diffuseColor / PI * terminator;

        result += (diffuse + specular) * light.Irradiance;
    }

    return result;
}

#endif
