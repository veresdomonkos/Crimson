#type vertex
#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

layout(set = 0, binding = 0) uniform CameraBlock {
    mat4 ViewProj;
    vec4 Position;
} u_Camera;

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec3 v_WorldPos;

void main()
{
    gl_Position = u_Camera.ViewProj * vec4(a_Position, 1.0);
    v_Normal = a_Normal;
    v_WorldPos = a_Position;
}

#type fragment
#version 450

const uint LIGHT_DIRECTIONAL = 0u;
const uint LIGHT_POINT       = 1u;
const uint LIGHT_SPOT        = 2u;
const uint MAX_LIGHTS        = 16u;

struct GPULight {
    vec4 PositionAndType;
    vec4 DirectionAndRange;
    vec4 ColorAndIntensity;
    vec4 SpotAngles;
};

layout(set = 0, binding = 0) uniform CameraBlock {
    mat4 ViewProj;
    vec4 Position;
} u_Camera;

layout(set = 0, binding = 1) uniform LightingBlock {
    vec4 AmbientColor;
    mat4 ShadowViewProj;
    uint LightCount;
    int ShadowLightIndex;
    uint _Pad0;
    uint _Pad1;
    GPULight Lights[MAX_LIGHTS];
} u_Lighting;

layout(set = 0, binding = 2) uniform sampler2D u_ShadowMap;

layout(set = 1, binding = 3) uniform MaterialBlock {
    vec4 u_Color;
} u_Material;

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec3 v_WorldPos;

layout(location = 0) out vec4 outColor;

float ComputeShadow(vec3 worldPos)
{
    vec4 lightSpacePos = u_Lighting.ShadowViewProj * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    projCoords.xy = projCoords.xy * 0.5 + vec2(0.5);

    if (projCoords.z > 1.0 || any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0))))
    return 1.0;

    float closestDepth = texture(u_ShadowMap, projCoords.xy).r;
    float bias = 0.005;
    return (projCoords.z - bias > closestDepth) ? 0.35 : 1.0;
}

void main()
{
    vec3 normal = normalize(v_Normal);
    vec3 result = u_Lighting.AmbientColor.rgb;

    for (uint i = 0u; i < u_Lighting.LightCount; ++i)
    {
        GPULight light = u_Lighting.Lights[i];
        uint type = uint(light.PositionAndType.w);

        vec3 lightVec;
        float attenuation = 1.0;

        if (type == LIGHT_DIRECTIONAL)
        {
            lightVec = -light.DirectionAndRange.xyz;
        }
        else
        {
            vec3 toLight = light.PositionAndType.xyz - v_WorldPos;
            float dist = length(toLight);
            lightVec = toLight / max(dist, 0.0001);

            float range = light.DirectionAndRange.w;
            attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
            attenuation *= attenuation;

            if (type == LIGHT_SPOT)
            {
                float cosAngle = dot(-lightVec, normalize(light.DirectionAndRange.xyz));
                float innerCos = light.SpotAngles.x;
                float outerCos = light.SpotAngles.y;
                attenuation *= clamp((cosAngle - outerCos) / max(innerCos - outerCos, 0.0001), 0.0, 1.0);
            }
        }

        float diff = max(dot(normal, normalize(lightVec)), 0.0);

        float shadow = 1.0;
        if (int(i) == u_Lighting.ShadowLightIndex)
        shadow = ComputeShadow(v_WorldPos);

        result += light.ColorAndIntensity.rgb * light.ColorAndIntensity.a * diff * attenuation * shadow;
    }

    outColor = vec4(u_Material.u_Color.rgb * result, 1.0);
}