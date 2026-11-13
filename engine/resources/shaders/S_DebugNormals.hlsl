// Debug shader: Shows the world space normal as a color, normal * 0.5 + 0.5.
// +X is red, +Y is green, +Z is blue; a normal pointing down -X shows as dark cyan, and so on.
// Back faces get the normal flipped to face the camera, as a double-sided surface would be shaded.
#include "crimson/transforms.hlsli"

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.ClipPosition = WorldToClip(ObjectToWorldPosition(input.Position));
    output.Normal = ObjectToWorldNormal(input.Normal);
    return output;
}

// TODO: No tonemapping
float4 PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    // Interpolation shortens the normal between vertices, so it is normalized again here
    float3 normal = normalize(input.Normal);
    if (!isFrontFace)
        normal = -normal;

    return float4(normal * 0.5 + 0.5, 1.0);
}
