#ifndef CRIMSON_BINDINGS_HLSLI
#define CRIMSON_BINDINGS_HLSLI

// The engine's standard resources, grouped by how often they change. Matches engine/include/crimson/renderer/frame_data.hpp.
//   space0  per frame     lights, shadows, object transforms, the shadow atlas and the shared samplers
//   space1  per pass      the camera
//   space2  per material  declared by each shader: one cbuffer and any number of textures

struct GpuLight
{
    float3 Position;
    uint Type;
    float3 Direction;
    float Range;
    float3 Color;
    float Intensity;
    float SpotInnerCos;
    float SpotOuterCos;
    int ShadowIndex;
    uint Pad;
};

struct GpuShadow
{
    float4x4 ViewProj;
    float4 AtlasRect;
    float DepthBias;
    float NormalBias;
    float TexelSize;
    float Pad;
};

struct ObjectData
{
    float4x4 Transform;
    float4x4 NormalMatrix;
};

cbuffer FrameBlock : register(b0, space0)
{
    float4 AmbientColor;
    uint LightCount;
    uint ShadowCount;
    uint2 FramePad;
};

StructuredBuffer<GpuLight> Lights : register(t0, space0);
StructuredBuffer<GpuShadow> Shadows : register(t1, space0);
StructuredBuffer<ObjectData> Objects : register(t2, space0);
Texture2D<float> ShadowAtlas : register(t3, space0);

SamplerState LinearWrap : register(s0, space0);
SamplerState LinearClamp : register(s1, space0);
SamplerState PointClamp : register(s2, space0);
SamplerComparisonState ShadowCompare : register(s3, space0);

cbuffer ViewBlock : register(b0, space1)
{
    float4x4 ViewProj;
    float4 CameraPosition;
};

struct DrawData
{
    uint ObjectIndex;
};

[[vk::push_constant]] ConstantBuffer<DrawData> Draw;

ObjectData GetObjectData()
{
    return Objects[Draw.ObjectIndex];
}

#endif
