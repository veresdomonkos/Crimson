#ifndef CRIMSON_NORMALS_HLSLI
#define CRIMSON_NORMALS_HLSLI

#include "crimson/math.hlsli"
#include "crimson/transforms.hlsli"

// The full unit tangent space normal from its X and Y, which are -1..1. Z is always positive, as a tangent space normal points out of the surface.
// Normalized, as filtered X and Y can be longer than 1.
float3 ReconstructNormalZ(float2 xy)
{
    return normalize(float3(xy, sqrt(saturate(1.0 - dot(xy, xy)))));
}

// Combines two tangent space normals so both sets of bumps show.
// Cheaper than reoriented blending.
float3 BlendNormalsDefault(float3 a, float3 b)
{
    return normalize(float3(a.xy + b.xy, a.z * b.z));
}

// Combines two tangent space normals and rotates the detail normal onto the base one, as if the detail sits on the base's surface.
// More costly than default blending.
float3 BlendNormalsReoriented(float3 base, float3 detail)
{
    const float3 t = base + float3(0.0, 0.0, 1.0);
    const float3 u = detail * float3(-1.0, -1.0, 1.0);
    return normalize(t * dot(t, u) / t.z - u);
}

// Converts a two-channel normal map texel to a unit direction in tangent space (-1..1).
// Only X and Y are stored, so Z is rebuilt from them.
float3 UnpackNormalXY(float4 texel)
{
    return ReconstructNormalZ(texel.xy * 2.0 - 1.0);
}

// Converts a normal map texel's 0..1 values to a unit direction in tangent space (-1..1).
// Use this after sampling a normal map to get correct direction vectors.
float3 UnpackNormalXYZ(float4 texel)
{
    return normalize(texel.xyz * 2.0 - 1.0);
}

// Scales how far a tangent space normal tilts: 0 is flat, 1 unchanged, above 1 exaggerated.
// Use this instead of multiplying by a scalar which would not change the direction.
// Normalized, as scaling X and Y changes the length.
float3 ApplyNormalStrength(float3 normal, float strength)
{
    return normalize(float3(normal.xy * strength, normal.z));
}

// Tilts a world space normal by a tangent space one, along the mesh's tangent.
float3 ApplyNormalMap(float3 normal, float4 tangent, float3 tangentNormal)
{
    // A mesh without UVs has w = 0 and nothing to go by
    if (abs(tangent.w) < 0.5)
        return normal;

    // Green points up in the image, and V goes down it, as the first row of an image is at V = 0
    const float3 flipped = float3(tangentNormal.x, -tangentNormal.y, tangentNormal.z);
    return normalize(TangentToWorldDirection(flipped, TangentFrame(normal, tangent)));
}

#endif
