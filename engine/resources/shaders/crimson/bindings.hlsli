#ifndef CRIMSON_BINDINGS_HLSLI
#define CRIMSON_BINDINGS_HLSLI

// The engine's standard resources, grouped by how often they change. Matches engine/include/crimson/renderer/frame_data.hpp.
//   space0  per frame     lights, shadows, object transforms, views, the shadow atlas and the shared samplers
//   space1  per material  declared by each shader: one cbuffer and any number of textures
// Each draw says which of the frame's objects and views it is drawn with, see GetObjectData() and GetView().

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

// A camera: the scene's in the main pass, a light's in the shadow pass
struct ViewData
{
    float4x4 ViewProj;
    float4 CameraPosition;
};

cbuffer FrameBlock : register(b0, space0)
{
    float4 AmbientColor;
    uint LightCount;
    uint ShadowCount;
    float Time;      // seconds since the first frame
    float DeltaTime; // seconds since the frame before
};

StructuredBuffer<GpuLight> Lights : register(t0, space0);
StructuredBuffer<GpuShadow> Shadows : register(t1, space0);
StructuredBuffer<ObjectData> Objects : register(t2, space0);
Texture2D<float> ShadowAtlas : register(t3, space0);
StructuredBuffer<ViewData> Views : register(t4, space0);

SamplerState LinearWrap : register(s0, space0);
SamplerState LinearClamp : register(s1, space0);
SamplerState PointClamp : register(s2, space0);
SamplerComparisonState ShadowCompare : register(s3, space0);

struct DrawData
{
    uint ObjectIndex;
    uint ViewIndex;
};

[[vk::push_constant]] ConstantBuffer<DrawData> Draw;

// The object being drawn
ObjectData GetObjectData()
{
    return Objects[Draw.ObjectIndex];
}

// The camera it is being drawn with
ViewData GetView()
{
    return Views[Draw.ViewIndex];
}

#endif
