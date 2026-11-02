#ifndef CRIMSON_LIGHTING_HLSLI
#define CRIMSON_LIGHTING_HLSLI

#include "crimson/bindings.hlsli"
#include "crimson/shadows.hlsli"

static const uint LIGHT_DIRECTIONAL = 0;
static const uint LIGHT_POINT = 1;
static const uint LIGHT_SPOT = 2;

// Ambient plus the diffuse light of every light at this point
float3 ComputeLighting(float3 worldPos, float3 normal)
{
    float3 result = AmbientColor.rgb;

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
            const float3 offset = light.Position - worldPos;
            const float distance = length(offset);
            toLight = offset / max(distance, 0.0001);

            attenuation = saturate(1.0 - distance / light.Range);
            attenuation *= attenuation;

            if (light.Type == LIGHT_SPOT)
            {
                const float cosAngle = dot(-toLight, normalize(light.Direction));
                attenuation *= saturate((cosAngle - light.SpotOuterCos) / max(light.SpotInnerCos - light.SpotOuterCos, 0.0001));
            }
        }

        const float diffuse = max(dot(normal, toLight), 0.0);
        if (diffuse <= 0.0 || attenuation <= 0.0)
            continue;

        float shadow = 1.0;
        if (light.ShadowIndex >= 0)
        {
            const uint face = light.Type == LIGHT_POINT ? GetPointShadowFace(-toLight) : 0;
            shadow = SampleShadow(light.ShadowIndex + face, worldPos, normal);
        }

        result += light.Color * light.Intensity * diffuse * attenuation * shadow;
    }

    return result;
}

#endif
