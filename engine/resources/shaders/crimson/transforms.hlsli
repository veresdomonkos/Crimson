#ifndef CRIMSON_TRANSFORMS_HLSLI
#define CRIMSON_TRANSFORMS_HLSLI

#include "crimson/bindings.hlsli"
#include "crimson/math.hlsli"

// Moving between the spaces of the object being drawn, the world and the camera's clip space.
// Directions keep the object's scale, and normals come out normalized.

float3 ObjectToWorldPosition(float3 position)
{
    return mul(GetObjectData().Transform, float4(position, 1.0)).xyz;
}

float3 ObjectToWorldDirection(float3 direction)
{
    return mul((float3x3)GetObjectData().Transform, direction);
}

float3 ObjectToWorldNormal(float3 normal)
{
    return normalize(mul((float3x3)GetObjectData().NormalMatrix, normal));
}

// NormalMatrix is the inverse of the transform, transposed. Multiplying from the other side transposes it back,
// which makes it the inverse: the way from the world into the object.
float3 WorldToObjectPosition(float3 position)
{
    return mul(float4(position, 1.0), GetObjectData().NormalMatrix).xyz;
}

float3 WorldToObjectDirection(float3 direction)
{
    return mul(direction, (float3x3)GetObjectData().NormalMatrix);
}

// A normal goes into the world by the inverse transposed transform, so the transposed transform brings it back
float3 WorldToObjectNormal(float3 normal)
{
    return normalize(mul(normal, (float3x3)GetObjectData().Transform));
}

float4 WorldToClip(float3 position)
{
    return mul(GetView().ViewProj, float4(position, 1.0));
}

float4 ObjectToClip(float3 position)
{
    return WorldToClip(ObjectToWorldPosition(position));
}

// Tangents follow the transform like any direction. A mirrored object flips which side the bitangent is on, so w flips with it.
float4 ObjectToWorldTangent(float4 tangent)
{
    const float3x3 transform = (float3x3)GetObjectData().Transform;
    return float4(mul(transform, tangent.xyz), tangent.w * sign(determinant(transform)));
}

// The unit direction from a point in the world to the camera
float3 GetViewDirection(float3 worldPosition)
{
    return normalize(GetView().CameraPosition.xyz - worldPosition);
}

// The surface's tangent space as rows: the tangent (along +U), the bitangent (along +V) and the normal.
// The tangent's w is the side the bitangent is on. Interpolation leaves the tangent a little off the normal, so it is made perpendicular again.
float3x3 TangentFrame(float3 normal, float4 tangent)
{
    const float3 t = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
    const float3 b = cross(normal, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
    return float3x3(t, b, normal);
}

float3 WorldToTangentDirection(float3 direction, float3x3 tangentFrame)
{
    return mul(tangentFrame, direction);
}

float3 TangentToWorldDirection(float3 direction, float3x3 tangentFrame)
{
    return mul(direction, tangentFrame);
}

// Where the object's origin is in the world
float3 ObjectWorldPosition()
{
    return ObjectToWorldPosition(float3(0.0, 0.0, 0.0));
}

#endif
