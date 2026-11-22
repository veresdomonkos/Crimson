#ifndef CRIMSON_COLOR_SPACE_HLSLI
#define CRIMSON_COLOR_SPACE_HLSLI

// Converting colors between primaries, transfer functions and ICtCp.
// Linear colors with absolute brightness are in cd/m^2 (nits) here, and a caller with units of its own converts to them.

// Between the primaries of Rec.709 (and sRGB) and Rec.2020, both with the D65 white, for linear colors
static const float3x3 REC709_TO_REC2020 = float3x3(
    0.627403896, 0.329283039, 0.043313065,
    0.069097289, 0.919540395, 0.011362316,
    0.016391439, 0.088013308, 0.895595253);

static const float3x3 REC2020_TO_REC709 = float3x3(
    1.660491002, -0.587641139, -0.072849863,
   -0.124550475, 1.132899898, -0.008349423,
   -0.018150763, -0.100578898, 1.118729661);

// The sRGB encoding of a linear color in 0-1
float3 EncodeSrgb(float3 linearColor)
{
    const float3 low = linearColor * 12.92;
    const float3 high = 1.055 * pow(max(linearColor, 0.0031308), 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(linearColor, 0.0031308));
}

// SMPTE ST 2084, the PQ curve of HDR10 and of ICtCp
static const float PQ_M1 = 0.1593017578125;
static const float PQ_M2 = 78.84375;
static const float PQ_C1 = 0.8359375;
static const float PQ_C2 = 18.8515625;
static const float PQ_C3 = 18.6875;
static const float PQ_MAX_NITS = 10000.0; // the brightness at PQ's 1.0

// From cd/m^2 to PQ's 0-1, going past 1 above PQ_MAX_NITS, for a display signal to clamp itself
float3 NitsToPq(float3 nits)
{
    const float3 ym = pow(max(nits / PQ_MAX_NITS, 0.0), PQ_M1);
    return pow((PQ_C1 + PQ_C2 * ym) / (1.0 + PQ_C3 * ym), PQ_M2);
}

// From PQ's 0-1 to cd/m^2
float3 PqToNits(float3 pq)
{
    const float3 np = pow(saturate(pq), 1.0 / PQ_M2);
    const float3 l = max(np - PQ_C1, 0.0) / (PQ_C2 - PQ_C3 * np);
    return pow(l, 1.0 / PQ_M1) * PQ_MAX_NITS;
}

// ICtCp (ITU-T T.302, Rec.2100), where I is brightness and Ct and Cp the color, with hue kept as brightness changes.
// From linear Rec.2020 in cd/m^2
float3 RgbToICtCp(float3 rgbNits)
{
    const float3x3 rgbToLms = float3x3(
        1688.0, 2146.0, 262.0,
        683.0, 2951.0, 462.0,
        99.0, 309.0, 3688.0) / 4096.0;
    const float3x3 lmsToICtCp = float3x3(
        2048.0, 2048.0, 0.0,
        6610.0, -13613.0, 7003.0,
        17933.0, -17390.0, -543.0) / 4096.0;

    return mul(lmsToICtCp, NitsToPq(mul(rgbToLms, rgbNits)));
}

// To linear Rec.2020 in cd/m^2, kept from going below 0.
// The coefficients are as rounded in Polyphony Digital's GT7 tone mapping sample, MIT licensed, see THIRD_PARTY_NOTICES.md
float3 ICtCpToRgb(float3 ictcp)
{
    const float3x3 ictcpToLms = float3x3(
        1.0, 0.00860904, 0.11103,
        1.0, -0.00860904, -0.11103,
        1.0, 0.560031, -0.320627);
    const float3x3 lmsToRgb = float3x3(
        3.43661, -2.50645, 0.0698454,
        -0.79133, 1.9836, -0.192271,
        -0.0259499, -0.0989137, 1.12486);

    return max(mul(lmsToRgb, PqToNits(mul(ictcpToLms, ictcp))), 0.0);
}

#endif
