// What the editor's ImGui is drawn with: a vertex color times a texture. The pass's view holds the projection of the
// display, and each draw names its texture by the property "Texture", so nothing here is a material value.
#include "crimson/bindings.hlsli"

Texture2D Texture : register(t0, space1);

struct VertexInput
{
    float2 Position : POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.ClipPosition = mul(GetView().ViewProj, float4(input.Position, 0.0, 1.0));
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    return input.Color * Texture.Sample(LinearClamp, input.UV);
}
