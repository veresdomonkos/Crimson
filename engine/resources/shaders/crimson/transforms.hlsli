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

float4 WorldToClip(float3 position)
{
    return mul(GetView().ViewProj, float4(position, 1.0));
}

float4 ObjectToClip(float3 position)
{
    return WorldToClip(ObjectToWorldPosition(position));
}

// Where the object's origin is in the world
float3 ObjectWorldPosition()
{
    return ObjectToWorldPosition(float3(0.0, 0.0, 0.0));
}

#endif
