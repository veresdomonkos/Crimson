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

// The light leaving the surface towards the camera, in linear light
float3 ComputeLighting(Surface surface)
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
        const GpuLight light = Lights[i];

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
            continue;

        float shadow = 1.0;
        if (light.ShadowIndex >= 0)
        {
            const uint face = light.Type == LIGHT_POINT ? GetPointShadowFace(-toLight) : 0;
            shadow = SampleShadow(light.ShadowIndex + face, surface.WorldPosition, surface.GeometricNormal);
        }

        const float3 halfVector = normalize(toLight + view);
        const float NdotH = saturate(dot(surface.Normal, halfVector));
        const float VdotH = saturate(dot(view, halfVector));

        const float3 fresnel = FresnelSchlick(VdotH, f0);
        const float3 specular = fresnel * DistributionGGX(NdotH, alpha) * VisibilitySmithGGX(NdotL, NdotV, alpha);
        const float3 diffuse = (1.0 - fresnel) * diffuseColor / PI;

        result += (diffuse + specular) * light.Color * light.Intensity * attenuation * shadow * NdotL;
    }

    return result;
}

#endif
