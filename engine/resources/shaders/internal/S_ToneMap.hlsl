// Drawn over the whole screen, turns the linear HDR scene into sRGB encoded colors for the display
#include "crimson/bindings.hlsli"
#include "crimson/color_space.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float Exposure;
    float ToneMapper; // SceneRenderer's ToneMapper enum
};

Texture2D SceneColor : register(t0, space1);

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 UV : TEXCOORD0;
};

// GT7's tone mapping, ported from Polyphony Digital's sample implementation, SDR only for now.
// https://blog.selfshadow.com/publications/s2025-shading-course/pdi/s2025_pbs_pdi_slides_v1.1.pdf
// Also listed in THIRD_PARTY_NOTICES.md.
//
// MIT License
//
// Copyright (c) 2025 Polyphony Digital Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// -----------------------------------------------------------------------------
// Defines the SDR reference white level used in our tone mapping (typically 250 nits).
// -----------------------------------------------------------------------------
static const float GT7_SDR_PAPER_WHITE = 250.0;       // cd/m^2

// -----------------------------------------------------------------------------
// Gran Turismo luminance-scale conversion helpers.
// In Gran Turismo, 1.0f in the linear frame-buffer space corresponds to
// REFERENCE_LUMINANCE cd/m^2 of physical luminance (typically 100 cd/m^2).
// -----------------------------------------------------------------------------
static const float GT7_REFERENCE_LUMINANCE = 100.0;   // cd/m^2

static const float GT7_PEAK = GT7_SDR_PAPER_WHITE / GT7_REFERENCE_LUMINANCE;
static const float GT7_PEAK_ICTCP = 0.602560759;      // the I of the peak's grey, RgbToICtCp(GT7_SDR_PAPER_WHITE).x
static const float GT7_BLEND_RATIO = 0.6;             // how much of the hue-keeping color there is in the result
static const float GT7_CHROMA_FADE_START = 0.98;      // saturation fades out between these, as fractions of the peak's I
static const float GT7_CHROMA_FADE_END = 1.16;

// The GT curve with GT7's shoulder, which converges on the peak, and GT7's parameters, per channel
float3 Gt7Curve(float3 color)
{
    const float peak = GT7_PEAK;
    const float alpha = 0.25;
    const float midPoint = 0.538;
    const float linearSection = 0.444;
    const float toeStrength = 1.28;

    const float k = (linearSection - 1.0) / (alpha - 1.0);
    const float kA = peak * linearSection + peak * k;
    const float kB = -peak * k * exp(linearSection / k);
    const float kC = -1.0 / (k * peak);

    const float3 x = max(color, 0.0);
    const float3 linearWeight = smoothstep(0.0, midPoint, x);
    const float3 toe = midPoint * pow(x / midPoint, toeStrength);
    const float3 belowShoulder = lerp(toe, x, linearWeight);
    const float3 shoulder = kA + kB * exp(x * kC);

    return lerp(belowShoulder, shoulder, step(linearSection * peak, x));
}

// Takes and gives linear Rec.709, the result in 0-1 for SDR
float3 ToneMapGt7(float3 color)
{
    const float3 rgb = mul(REC709_TO_REC2020, color);
    const float3 ucs = RgbToICtCp(rgb * GT7_REFERENCE_LUMINANCE);

    const float3 skewedRgb = Gt7Curve(rgb);
    const float3 skewedUcs = RgbToICtCp(skewedRgb * GT7_REFERENCE_LUMINANCE);

    const float chromaScale = 1.0 - smoothstep(GT7_CHROMA_FADE_START, GT7_CHROMA_FADE_END, ucs.x / GT7_PEAK_ICTCP);
    const float3 scaledRgb = ICtCpToRgb(float3(skewedUcs.x, ucs.yz * chromaScale)) / GT7_REFERENCE_LUMINANCE;

    const float3 blended = lerp(skewedRgb, scaledRgb, GT7_BLEND_RATIO);

    // The peak is SDR's white, which sRGB has at 1.0
    return mul(REC2020_TO_REC709, min(blended, GT7_PEAK) / GT7_PEAK);
}

VertexOutput VSMain(float3 position : POSITION)
{
    VertexOutput output;
    output.ClipPosition = float4(position, 1.0);
    output.UV = position.xy * 0.5 + 0.5;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 scene = SceneColor.Sample(PointClamp, input.UV).rgb * Exposure;
    const float3 mapped = ToneMapper < 0.5 ? ToneMapGt7(scene) : scene;

    return float4(EncodeSrgb(saturate(mapped)), 1.0); // SDR output only currently (0-1 means SDR - saturate)
}
