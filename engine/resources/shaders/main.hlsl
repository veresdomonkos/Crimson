// The default lit shader: metallic-roughness PBR, with the same material slots as glTF.
// Colors are linear. Turn sRGB off on the textures in every slot but ColorMap and EmissiveMap.
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color; // @default 1 1 1 1
    float Metallic; // @default 0 @range 0 1
    float Roughness; // @default 0.5 @range 0 1
    float NormalStrength; // @default 1 @range 0 2
    float OcclusionStrength; // @default 1 @range 0 1
    float3 EmissiveColor; // @default 0 0 0
    float EmissiveStrength; // @default 1
};

Texture2D ColorMap : register(t0, space2);
Texture2D NormalMap : register(t1, space2); // @default normal
Texture2D MetallicRoughnessMap : register(t2, space2); // green is roughness, blue is metallic
Texture2D OcclusionMap : register(t3, space2); // red
Texture2D EmissiveMap : register(t4, space2);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const ObjectData object = GetObjectData();
    const float4 worldPosition = mul(object.Transform, float4(input.Position, 1.0));

    VertexOutput output;
    output.ClipPosition = mul(ViewProj, worldPosition);
    output.Normal = mul((float3x3)object.NormalMatrix, input.Normal);
    output.WorldPosition = worldPosition.xyz;
    output.UV = input.UV;
    return output;
}

// Meshes have no tangents, so the normal map's directions come from how position and UV change between pixels
float3 ApplyNormalMap(float3 normal, float3 worldPosition, float2 uv)
{
    float3 mapped = NormalMap.Sample(LinearWrap, uv).xyz * 2.0 - 1.0;
    mapped.xy *= NormalStrength;

    const float3 dpx = ddx(worldPosition);
    const float3 dpy = ddy(worldPosition);
    const float2 dux = ddx(uv);
    const float2 duy = ddy(uv);

    const float3 perpY = cross(dpy, normal);
    const float3 perpX = cross(normal, dpx);

    const float3 tangent = perpY * dux.x + perpX * duy.x;
    const float3 bitangent = perpY * dux.y + perpX * duy.y;

    // A mesh without UVs has nothing to go by
    const float scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
    const float determinant = dot(dpx, perpY);
    if (scale <= 0.0 || determinant == 0.0)
        return normal;

    // The determinant's sign has to stay, as the backends rasterize with Y going opposite ways
    const float invScale = sign(determinant) / sqrt(scale);

    // Green points up in the image, and V goes down it, as the first row of an image is at V = 0
    return normalize(tangent * invScale * mapped.x - bitangent * invScale * mapped.y + normal * mapped.z);
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float4 metallicRoughness = MetallicRoughnessMap.Sample(LinearWrap, input.UV);
    const float occlusion = OcclusionMap.Sample(LinearWrap, input.UV).r;
    const float3 emissive = EmissiveColor * EmissiveStrength * EmissiveMap.Sample(LinearWrap, input.UV).rgb;

    const float3 geometricNormal = normalize(input.Normal);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = ApplyNormalMap(geometricNormal, input.WorldPosition, input.UV);
    surface.GeometricNormal = geometricNormal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic * metallicRoughness.b;
    surface.Roughness = Roughness * metallicRoughness.g;
    surface.Occlusion = lerp(1.0, occlusion, OcclusionStrength);

    return float4(ComputeLighting(surface) + emissive, base.a);
}
