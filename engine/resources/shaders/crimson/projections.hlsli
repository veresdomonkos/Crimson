#ifndef CRIMSON_PROJECTIONS_HLSLI
#define CRIMSON_PROJECTIONS_HLSLI

#include "crimson/math.hlsli"
#include "crimson/normals.hlsli"

// A texture sampled with a planar projection, U along uAxis and V along vAxis.
// V goes down the image (its first row is at V = 0), so pass the direction that should be down in the image as vAxis, such as -up.
float4 SamplePlanar(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 uAxis, float3 vAxis, float scale)
{
    return tex.Sample(samplerState, float2(dot(worldPosition, uAxis), dot(worldPosition, vAxis)) * scale);
}

// Like SampleTriplanarWorld, but with only the two axes the surface faces most: two samples instead of three, for nearly the same result: https://iquilezles.org/articles/biplanar/
// The projections are laid out like SampleTriplanarWorld's, so a material can switch between the two without the texture turning.
// The chosen axes can change between neighbouring pixels, so the samples use explicit gradients to keep mip selection seamless.
// Sharpness works like in TriplanarWeights; around 8 is a good start.
float4 SampleBiplanarWorld(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness)
{
    const float3 p = worldPosition * scale;
    const float3 dpdx = ddx(p);
    const float3 dpdy = ddy(p);
    const float3 n = abs(worldNormal);
    const float3 axisSign = float3(worldNormal.x < 0.0 ? -1.0 : 1.0, worldNormal.y < 0.0 ? -1.0 : 1.0, worldNormal.z < 0.0 ? -1.0 : 1.0);

    // The axis the surface faces most, the one it faces least, and the one in between.
    // The least uses <=, so equal components still give three different axes.
    const int major = (n.x > n.y && n.x > n.z) ? 0 : (n.y > n.z) ? 1 : 2;
    const int minor = (n.x <= n.y && n.x <= n.z) ? 0 : (n.y <= n.z) ? 1 : 2;
    const int median = 3 - major - minor;

    // Each axis's UV and its gradients, with the same mirroring and V direction as SampleTriplanarWorld.
    float2 uv[3] = { float2(-p.z * axisSign.x, -p.y), float2(-p.x * axisSign.y, -p.z), float2(p.x * axisSign.z, -p.y) };
    float2 uvdx[3] = { float2(-dpdx.z * axisSign.x, -dpdx.y), float2(-dpdx.x * axisSign.y, -dpdx.z), float2(dpdx.x * axisSign.z, -dpdx.y) };
    float2 uvdy[3] = { float2(-dpdy.z * axisSign.x, -dpdy.y), float2(-dpdy.x * axisSign.y, -dpdy.z), float2(dpdy.x * axisSign.z, -dpdy.y) };

    const float4 x = tex.SampleGrad(samplerState, uv[major], uvdx[major], uvdy[major]);
    const float4 y = tex.SampleGrad(samplerState, uv[median], uvdx[median], uvdy[median]);

    // 1 / sqrt(3) is the smallest the largest normal component can be, and the largest the median one can be when it swaps with the minor one.
    // Remapping from there makes the weaker projection fade out completely before its axis would switch, which hides the change of axes.
    float2 weights = float2(n[major], n[median]);
    weights = saturate((weights - INV_SQRT3) / (1.0 - INV_SQRT3));
    weights = pow(weights, sharpness / 8.0);

    // On the exact diagonal both weights are 0, so the major one is kept above it to not divide by zero
    weights.x = max(weights.x, EPSILON);

    return (x * weights.x + y * weights.y) / (weights.x + weights.y);
}

// The same, projected in the object's own space: the texture moves, turns and stretches with the object, as for props.
// Takes the world position and normal like SampleBiplanarWorld, so a shader can switch between the two.
float4 SampleBiplanarObject(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness)
{
    return SampleBiplanarWorld(tex, samplerState, WorldToObjectPosition(worldPosition), WorldToObjectNormal(worldNormal), scale, sharpness);
}

// A normal map sampled from the two axis projections the surface faces most, returned in the same space as the normal given (no ApplyNormalMap needed).
// The same axes, UVs and weights as SampleBiplanarWorld, and each projection is turned like in SampleTriplanarNormalWorld.
float3 SampleBiplanarNormalWorld(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness, float strength)
{
    const float3 p = worldPosition * scale;
    const float3 dpdx = ddx(p);
    const float3 dpdy = ddy(p);
    const float3 n = abs(worldNormal);
    const float3 axisSign = float3(worldNormal.x < 0.0 ? -1.0 : 1.0, worldNormal.y < 0.0 ? -1.0 : 1.0, worldNormal.z < 0.0 ? -1.0 : 1.0);

    const int major = (n.x > n.y && n.x > n.z) ? 0 : (n.y > n.z) ? 1 : 2;
    const int minor = (n.x <= n.y && n.x <= n.z) ? 0 : (n.y <= n.z) ? 1 : 2;
    const int median = 3 - major - minor;

    float2 uv[3] = { float2(-p.z * axisSign.x, -p.y), float2(-p.x * axisSign.y, -p.z), float2(p.x * axisSign.z, -p.y) };
    float2 uvdx[3] = { float2(-dpdx.z * axisSign.x, -dpdx.y), float2(-dpdx.x * axisSign.y, -dpdx.z), float2(dpdx.x * axisSign.z, -dpdx.y) };
    float2 uvdy[3] = { float2(-dpdy.z * axisSign.x, -dpdy.y), float2(-dpdy.x * axisSign.y, -dpdy.z), float2(dpdy.x * axisSign.z, -dpdy.y) };

    float2 weights = float2(n[major], n[median]);
    weights = saturate((weights - INV_SQRT3) / (1.0 - INV_SQRT3));
    weights = pow(weights, sharpness / 8.0);
    weights.x = max(weights.x, EPSILON);

    int axes[2] = { major, median };
    float3 result = float3(0.0, 0.0, 0.0);

    [unroll]
    for (int i = 0; i < 2; ++i)
    {
        const int axis = axes[i];
        float3 t = ApplyNormalStrength(UnpackNormalXYZ(tex.SampleGrad(samplerState, uv[axis], uvdx[axis], uvdy[axis])), strength);

        // Where U was mirrored, the normal's X is too.
        // Then the whiteout blend with the surface normal in the projection's swizzled space, swizzled back onto the axes, as in SampleTriplanarNormalWorld.
        float3 blended;
        if (axis == 0)
        {
            t.x *= -axisSign.x;
            blended = float3(abs(t.z) * worldNormal.x, t.y + worldNormal.y, t.x + worldNormal.z);
        }
        else if (axis == 1)
        {
            t.x *= -axisSign.y;
            blended = float3(t.x + worldNormal.x, abs(t.z) * worldNormal.y, t.y + worldNormal.z);
        }
        else
        {
            t.x *= axisSign.z;
            blended = float3(t.x + worldNormal.x, t.y + worldNormal.y, abs(t.z) * worldNormal.z);
        }

        result += blended * weights[i];
    }

    return normalize(result);
}

// The same, projected in the object's own space, so the bumps stay with SampleBiplanarObject's color. Returns a world space normal.
float3 SampleBiplanarNormalObject(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness, float strength)
{
    const float3 objectNormal = SampleBiplanarNormalWorld(tex, samplerState, WorldToObjectPosition(worldPosition), WorldToObjectNormal(worldNormal), scale, sharpness, strength);
    return ObjectToWorldNormal(objectNormal);
}

// How much each axis projection counts, from the normal.
// Sharpness above 1 narrows the blend zone between projections; 1 blends broadly, around 4 to 8 keeps the transitions tight.
float3 TriplanarWeights(float3 normal, float sharpness)
{
    const float3 weights = pow(abs(normal), sharpness);
    return weights / (weights.x + weights.y + weights.z);
}

// A texture sampled from the three axis projections in world space, and blended.
// The texture stays put in the world and objects move through it, so neighbouring objects line up, as for terrain and walls.
float4 SampleTriplanarWorld(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness)
{
    const float3 p = worldPosition * scale;
    const float3 weights = TriplanarWeights(worldNormal, sharpness);

    // -1 or 1, not sign(), whose 0 on an exact axis would squash the UVs
    const float3 axisSign = float3(worldNormal.x < 0.0 ? -1.0 : 1.0, worldNormal.y < 0.0 ? -1.0 : 1.0, worldNormal.z < 0.0 ? -1.0 : 1.0);

    // U is mirrored on the sides where the texture would otherwise read backwards, for a right-handed space (Unity's left-handed examples have the opposite signs).
    // V runs against the axis, as V goes down the image (its first row is at V = 0), so up the image is up the axis.
    const float4 x = tex.Sample(samplerState, float2(-p.z * axisSign.x, -p.y));
    const float4 y = tex.Sample(samplerState, float2(-p.x * axisSign.y, -p.z));
    const float4 z = tex.Sample(samplerState, float2( p.x * axisSign.z, -p.y));

    return x * weights.x + y * weights.y + z * weights.z;
}

// The same, projected in the object's own space: the texture moves, turns and stretches with the object, as for props.
// Takes the world position and normal like SampleTriplanarWorld, so a shader can switch between the two.
float4 SampleTriplanarObject(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness)
{
    return SampleTriplanarWorld(tex, samplerState, WorldToObjectPosition(worldPosition), WorldToObjectNormal(worldNormal), scale, sharpness);
}

// A normal map sampled from the three axis projections, returned in the same space as the normal given (no ApplyNormalMap needed).
// Each projection's tangent space normal is combined with the surface normal (whiteout blending) and swizzled onto its axis.
// The same UVs as SampleTriplanarWorld, so the bumps line up with the color.
float3 SampleTriplanarNormalWorld(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness, float strength)
{
    const float3 p = worldPosition * scale;
    const float3 weights = TriplanarWeights(worldNormal, sharpness);
    const float3 axisSign = float3(worldNormal.x < 0.0 ? -1.0 : 1.0, worldNormal.y < 0.0 ? -1.0 : 1.0, worldNormal.z < 0.0 ? -1.0 : 1.0);

    float3 tangentX = ApplyNormalStrength(UnpackNormalXYZ(tex.Sample(samplerState, float2(-p.z * axisSign.x, -p.y))), strength);
    float3 tangentY = ApplyNormalStrength(UnpackNormalXYZ(tex.Sample(samplerState, float2(-p.x * axisSign.y, -p.z))), strength);
    float3 tangentZ = ApplyNormalStrength(UnpackNormalXYZ(tex.Sample(samplerState, float2( p.x * axisSign.z, -p.y))), strength);

    // Where U was mirrored, the normal's X is too. Green needs no flip: it points up the image, which the UVs made up the axis.
    tangentX.x *= -axisSign.x;
    tangentY.x *= -axisSign.y;
    tangentZ.x *= axisSign.z;

    // Whiteout blend of each projection with the surface normal, in that projection's swizzled space
    tangentX = float3(tangentX.xy + worldNormal.zy, abs(tangentX.z) * worldNormal.x);
    tangentY = float3(tangentY.xy + worldNormal.xz, abs(tangentY.z) * worldNormal.y);
    tangentZ = float3(tangentZ.xy + worldNormal.xy, abs(tangentZ.z) * worldNormal.z);

    // Swizzled back onto the axes and blended
    return normalize(tangentX.zyx * weights.x + tangentY.xzy * weights.y + tangentZ.xyz * weights.z);
}

// The same, projected in the object's own space, so the bumps stay with SampleTriplanarObject's color. Returns a world space normal.
float3 SampleTriplanarNormalObject(Texture2D tex, SamplerState samplerState, float3 worldPosition, float3 worldNormal, float scale, float sharpness, float strength)
{
    const float3 objectNormal = SampleTriplanarNormalWorld(tex, samplerState, WorldToObjectPosition(worldPosition), WorldToObjectNormal(worldNormal), scale, sharpness, strength);
    return ObjectToWorldNormal(objectNormal);
}

#endif
