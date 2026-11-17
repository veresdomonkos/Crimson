#ifndef CRIMSON_CALLISTO_PRESETS_HLSLI
#define CRIMSON_CALLISTO_PRESETS_HLSLI

// The Callisto controls the talk fitted to ten materials of the MERL database.
// The tints are converted from the slides' sRGB to linear.
// Each one only looks like its material with the surface the talk fitted alongside it, which isn't part of CallistoParams:
// the comment above each function has its base color (sRGB), roughness and metallic.
// TODO: specular, dielectric's reflectance

// Defaults to Lambert.
CallistoParams DefaultCallistoParams()
{
    CallistoParams params;
    params.DiffuseFresnel = float3(1.0, 1.0, 1.0);
    params.DiffuseFresnelFalloff = 0.75;
    params.DiffuseFresnelTangentFalloff = 0.75;
    params.Retroreflection = float3(1.0, 1.0, 1.0);
    params.RetroreflectionFalloff = 0.75;
    params.RetroreflectionTangentFalloff = 0.75;
    params.SmoothTerminator = 0.0;
    params.SmoothTerminatorLength = 0.5;
    params.SpecularFresnelFalloff = 0.5;
    params.DualSpecularRoughnessScale = 2.0;
    params.DualSpecularOpacity = 0.0;
    return params;
}

// Base color (20, 20, 20), roughness 0.6
CallistoParams CallistoBlackSoftPlastic()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = float3(175.0, 175.0, 175.0);
    params.DiffuseFresnelFalloff = 0.7;
    params.Retroreflection = float3(3.0, 3.0, 3.0);
    params.RetroreflectionFalloff = 0.7;
    params.SpecularFresnelFalloff = 0.0;
    return params;
}

// Base color (40, 42, 66), roughness 0.77
CallistoParams CallistoBlueFabric()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = 40.0 * float3(1.0, 0.7011, 0.4851);
    params.DiffuseFresnelFalloff = 0.6;
    params.DiffuseFresnelTangentFalloff = 0.95;
    params.Retroreflection = float3(3.0, 3.0, 3.0);
    params.RetroreflectionFalloff = 0.5;
    params.SpecularFresnelFalloff = 0.0;
    return params;
}

// Base color (152, 130, 93), roughness 0.055
CallistoParams CallistoYellowPhenolic()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = float3(0.6, 0.6, 0.6);
    params.DiffuseFresnelFalloff = 0.85;
    params.DiffuseFresnelTangentFalloff = 1.0;
    params.Retroreflection = float3(0.3, 0.3, 0.3);
    params.RetroreflectionFalloff = 0.85;
    params.RetroreflectionTangentFalloff = 1.0;
    params.SmoothTerminator = 0.25;
    params.DualSpecularRoughnessScale = 4.25;
    params.DualSpecularOpacity = 0.15;
    return params;
}

// Base color (128, 124, 55), roughness 0.5
CallistoParams CallistoYellowPlastic()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = 5.0 * float3(1.0, 0.5776, 1.0);
    params.DiffuseFresnelFalloff = 0.6;
    params.DiffuseFresnelTangentFalloff = 0.85;
    params.Retroreflection = 3.5 * float3(0.7529, 0.521, 1.0);
    params.RetroreflectionFalloff = 0.25;
    params.RetroreflectionTangentFalloff = 1.0;
    params.SpecularFresnelFalloff = 0.375;
    return params;
}

// Base color (105, 96, 88), roughness 0.21, metallic 0.85
CallistoParams CallistoGoldMetallicPaint()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnelFalloff = 0.5;
    params.DiffuseFresnelTangentFalloff = 1.0;
    params.RetroreflectionFalloff = 0.5;
    params.SpecularFresnelFalloff = 0.0;
    params.DualSpecularOpacity = 0.9;
    return params;
}

// Base color (173, 58, 23), roughness 0.6
CallistoParams CallistoLightRedPaint()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnelFalloff = 0.5;
    params.DiffuseFresnelTangentFalloff = 1.0;
    params.Retroreflection = 6.5 * float3(0.5776, 0.8714, 1.0);
    params.RetroreflectionFalloff = 0.5;
    params.RetroreflectionTangentFalloff = 0.65;
    params.SmoothTerminator = 0.2;
    params.SpecularFresnelFalloff = 0.4;
    return params;
}

// Base color (106, 29, 8), roughness 1
CallistoParams CallistoRedFabric()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = 25.0 * float3(0.4564, 0.3515, 1.0);
    params.DiffuseFresnelFalloff = 0.6;
    params.DiffuseFresnelTangentFalloff = 0.97;
    params.Retroreflection = 1.25 * float3(1.0, 0.0, 1.0);
    params.RetroreflectionFalloff = 0.5;
    params.SmoothTerminator = -0.1;
    return params;
}

// Base color (154, 57, 32), roughness 0.03
CallistoParams CallistoSpecularRedPhenolic()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = float3(0.4, 0.4, 0.4);
    params.DiffuseFresnelFalloff = 0.9;
    params.DiffuseFresnelTangentFalloff = 1.0;
    params.Retroreflection = float3(0.5, 0.5, 0.5);
    params.RetroreflectionFalloff = 0.8;
    params.RetroreflectionTangentFalloff = 0.95;
    params.SmoothTerminator = 0.2;
    params.DualSpecularRoughnessScale = 4.0;
    params.DualSpecularOpacity = 0.125;
    return params;
}

// Base color (250, 250, 250), roughness 0.027, metallic 0.97
CallistoParams CallistoTwoLayerSilver()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnelFalloff = 0.5;
    params.DiffuseFresnelTangentFalloff = 1.0;
    params.RetroreflectionFalloff = 0.5;
    params.DualSpecularRoughnessScale = 14.0;
    params.DualSpecularOpacity = 0.18;
    return params;
}

// Base color (148, 97, 48), roughness 0.165
CallistoParams CallistoYellowMattePlastic()
{
    CallistoParams params = DefaultCallistoParams();
    params.DiffuseFresnel = float3(0.25, 0.25, 0.25);
    params.DiffuseFresnelTangentFalloff = 1.0;
    params.Retroreflection = float3(0.6, 0.6, 0.6);
    params.RetroreflectionFalloff = 0.85;
    params.RetroreflectionTangentFalloff = 1.0;
    params.SmoothTerminator = 0.25;
    params.SpecularFresnelFalloff = 0.25;
    params.DualSpecularOpacity = 0.9;
    return params;
}

#endif
