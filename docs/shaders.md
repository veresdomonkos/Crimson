# Writing shaders

Shaders are HLSL, with a `VSMain` and a `PSMain` in one `.hlsl` file. The same file runs on both backends:

```
.hlsl -> glslang -> SPIR-V -> Vulkan
                           -> SPIRV-Cross -> GLSL 4.5 -> OpenGL
```

glslang is linked into the engine, so shaders compile in the editor as they are saved. The SPIR-V it makes is legalized and validated with SPIRV-Tools before either backend gets it, so a texture can be passed to a function, as in `Fetch(Texture2D map, float2 uv)`. Its HLSL support is around Shader Model 5, so Shader Model 6 features such as wave ops and mesh shaders aren't available.

## Making one

**New Shader** in the Assets panel's right-click menu makes a lit shader to start from, with a color, metallic and roughness, and a color map. Give a material that shader, and the material's Inspector shows the properties and textures the shader declares.

- **Hot reload:** a project's shader reloads within half a second of being saved, along with its materials. If the new version doesn't compile, the error shows in the corner of the window and in the Console, and the last version that compiled stays.
- **Opening it:** double-click it in the Assets panel. That opens it in the **External Script Editor** set in **Edit > Preferences**: VS Code, Visual Studio, Rider, CLion or any program. VS Code opens the project as its workspace, so the [HLSL Tools](https://marketplace.visualstudio.com/items?itemName=TimGJones.hlsltools) extension finds the engine's headers through the project's `shadertoolsconfig.json`.
- **Engine shaders** are built into the editor, so changing one means rebuilding it.

## What a shader declares

Resources are grouped by how often they change:

| Space | Scope | Resources |
|:------|:------|:----------|
| `space0` | per frame | `FrameBlock` (`AmbientColor`, `Time` and `DeltaTime` in seconds), `Lights`, `Shadows`, `Objects`, `Views`, `ShadowAtlas` and the samplers `LinearWrap`, `LinearClamp`, `PointClamp`, `ShadowCompare` |
| `space1` | per material, **optional** | one constant buffer (`b0`) and any textures (`t0` and up), both looked up by name |
| push constant | per draw | `DrawData` (`ObjectIndex`, `ViewIndex`) |

A draw's object and camera are entries of the frame's `Objects` and `Views`, picked by the draw's indices. `GetObjectData()` returns the object's `Transform` and `NormalMatrix`, and `GetView()` the camera's `ViewProj` and `CameraPosition`, in either stage. The camera is the scene's in the main pass and a light's in the shadow pass, so a shader never reads `Views` by an index of its own. `WorldToClip` and the other helpers in `crimson/transforms.hlsli` already go through them.

`crimson/bindings.hlsli` declares the standard ones, so shaders include it instead of restating them, and a material shader declares only its own:

```hlsl
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float4 Color; // @default 1 1 1 1
    float Roughness; // @default 0.5 @range 0 1
};

Texture2D Albedo : register(t0, space1);
Texture2D Bumps : register(t1, space1); // @default normal

// VSMain uses GetObjectData() and GetView(), PSMain fills in a Surface for ComputeLightingBurley() and uses Albedo.Sample(LinearWrap, uv)
```

- **Discovery:** material properties and textures are found with SPIRV-Reflect, so the shader is the only place they're declared. The standard blocks are checked against the engine's structs.
- **Unused textures:** a texture the shader declares but never samples still shows as a material texture. Reflection reads the SPIR-V before the unused resources are removed.
- **Samplers:** materials share the samplers in `space0`, and can't declare their own.
- **Import errors:** a shader fails to import, with an error in the console, if it uses an unknown space or a standard resource whose type or size doesn't match the engine.
- **Vertex inputs:** meshes give `POSITION` (float3), `NORMAL` (float3), `TEXCOORD0` (float2) and `TANGENT` (float4: xyz along +U, w the side the bitangent is on, ±1, and 0 for a mesh without UVs). There are no vertex colors. Inputs take their locations from their order in the input struct, so declare them in this order; a shader can leave out the ones at the end.

### Annotations

The comment after a declaration says what materials start with:

| Annotation | On | Does |
|:-----------|:---|:-----|
| `@default 1 0.5 0` | a property | the value a material has until it sets its own. Without one, a property starts at zero. |
| `@range 0 1` | a number or a vector | a slider in the Inspector, between those values. On a vector, each component is kept between them. |
| `@default normal` | a texture | an empty slot samples a flat normal. Without it, an empty slot samples white. |

A material property that's back at its `@default` isn't written to the `.mat` file, and **Reset to default** in the Inspector puts it there.

A draw can also carry `MaterialProperties`, which set values and textures for that one draw, on top of its material's, without making a material. They are named by the property's name or by a `PropertyId`, and a name the draw's shader doesn't declare is ignored. The editor's UI draws every image this way, with one material and a texture for each draw.

## The headers

Includes resolve relative to the shader's folder, then to the engine's shaders.

| Header | Has |
|:-------|:----|
| `crimson/bindings.hlsli` | the standard resources above, and `GetObjectData()` for the current draw |
| `crimson/math.hlsli` | `PI`, `TAU`, `HALF_PI`, `INV_PI`, `E`, `EPSILON`, `INV_SQRT3`, `FLT_MIN` and `FLT_MAX`, and `Pow5` and `SafePow`, a `pow` that gives 1 rather than NaN for 0 to the power of 0 |
| `crimson/transforms.hlsli` | `ObjectToWorldPosition`, `ObjectToWorldDirection`, `ObjectToWorldNormal`, `ObjectToWorldTangent`, `WorldToObjectPosition`, `WorldToObjectDirection`, `WorldToObjectNormal`, `WorldToClip`, `ObjectToClip`, `ObjectWorldPosition()`, `GetViewDirection`, and `TangentFrame` with `WorldToTangentDirection` and `TangentToWorldDirection` |
| `crimson/normals.hlsli` | unpacking normal maps (`UnpackNormalXYZ`, `UnpackNormalXY`, `ReconstructNormalZ`), `ApplyNormalStrength`, `ApplyNormalMap` and two ways to blend normals, `BlendNormalsDefault` and `BlendNormalsReoriented` |
| `crimson/projections.hlsli` | texturing without UVs: `SamplePlanar`, and biplanar and triplanar projection in the world or the object (`SampleTriplanarObject`, `SampleBiplanarWorld`, `SampleTriplanarNormalObject` and so on) |
| `crimson/parallax.hlsli` | `ParallaxOcclusion`, which finds the UV where the view ray meets a height map |
| `crimson/lighting.hlsli` | `Surface`, and the light leaving a point on a surface with the scene's lights, shadows and ambient light: `ComputeLightingBurley(Surface)` with Burley's diffuse, and `ComputeLightingCallisto(Surface, CallistoParams)` with the [Callisto BRDF](#s_litcallisto-and-s_litmaskcallisto). `SampleLight` is how much of one light reaches the point, for a lighting function of your own. Includes `bindings`, `brdf`, `math` and `transforms`. |
| `crimson/brdf.hlsli` | the BRDFs' parts: `DistributionGGX`, `VisibilitySmithGGX`, `FresnelSchlick`, `EnvironmentBRDF` and `DiffuseBurley`, and for the Callisto BRDF `CallistoParams`, `DiffuseCallisto`, `CallistoTerminator` and `FresnelSchlickFalloff` |
| `crimson/callisto_presets.hlsli` | `CallistoParams` to start from: `DefaultCallistoParams()`, and the values the Callisto talk fitted to ten measured materials, such as `CallistoBlueFabric()` and `CallistoTwoLayerSilver()` |

## Built-in shaders and materials

The engine ships shaders in `engine/shaders/` and materials that use them in `engine/materials/`. They show under the **Engine** root once **Show Engine Content** is on. Every material works as Opaque, Cutout or Transparent, with any cull mode.

### S_Lit

`engine/shaders/S_Lit.hlsl` is the default lit shader, used by `M_Default`: metallic-roughness PBR with a Cook-Torrance GGX specular and Burley's diffuse, with the same material slots as glTF. Unlike Lambert's, Burley's diffuse depends on roughness: a rough surface looks flatter and brighter at grazing angles, and a smooth one darker there. It is normalized as in Frostbite, so a rough surface doesn't reflect more light than it receives. It has nothing to tune.

| Property | Default | |
|:---------|:--------|:--|
| `Color` | white | linear, multiplied with `ColorMap` |
| `Metallic`, `Roughness` | 0, 0.5 | multiplied with `MetallicRoughnessMap` (blue and green), as in glTF |
| `NormalStrength` | 1 | scales `NormalMap`, with green pointing up in the image (the OpenGL convention, which glTF uses too) |
| `OcclusionStrength` | 1 | how much `OcclusionMap` (red) darkens ambient light |
| `EmissiveColor`, `EmissiveStrength` | black, 1 | linear, multiplied with `EmissiveMap` |
| `AlphaCutoff` | 0.5 | for a cutout material, where the alpha of `Color` times `ColorMap` is under it there is nothing |
| `Tiling` | 1, 1 | multiplies the UVs, to repeat the textures |

Normal maps use the mesh's tangents, which the importer works out with MikkTSpace, the same way Blender, Substance and other baking tools do, so baked normal maps match. A mesh without UVs has no tangents to go by and stays flat.

### The other lit shaders

These share `S_Lit`'s lighting, `Color`, `NormalStrength`, `OcclusionStrength`, `EmissiveColor`, `EmissiveStrength` and `AlphaCutoff`, and use the same slots for `ColorMap`, `NormalMap` and `EmissiveMap`. Each is its own file with the surface code written out, so a change to how a surface is lit has to be made in all of them.

- **`S_LitMask`** (`M_LitMask`) takes its metallic, occlusion and roughness from one `MaskMap`, packed like Unity HDRP's mask map but with roughness, not smoothness, in alpha: red is metallic, green is occlusion and alpha is roughness. `Metallic` and `Roughness` default to 1 and scale what the mask holds, so without a mask map it is a fully rough metal. It has no `MetallicRoughnessMap` or `OcclusionMap`. Turn sRGB off on `NormalMap` and `MaskMap`.
- **`S_LitMaskParallax`** is `S_LitMask` with parallax occlusion mapping. `HeightMap` (white is the top, sRGB off) is marched along the view ray, so the surface looks sunken where it is dark. `ParallaxHeightStrength` is how deep, and `ParallaxSteps` is the most steps the march takes. The shadows and the selection outline use the flat surface.
- **`S_LitMaskTriplanar` and `S_LitMaskBiplanar`** are `S_LitMask` with its maps projected along the object's axes instead of read at its UVs, for meshes without good UVs, such as rocks and blockouts. They project in the object's own space, so the texture moves and stretches with the object. `ProjectionTiling` is how many times a map repeats per unit, and `TriplanarSharpness` or `BiplanarSharpness` is how narrow the blends between the axes are. Biplanar takes two samples per map instead of three, for nearly the same look. `Tiling` doesn't apply, as there are no UVs.

The parallax and projected shaders have no material of their own. Pick them as the shader of a material you make.

### S_LitCallisto and S_LitMaskCallisto

`S_LitCallisto` (`M_LitCallisto`) and `S_LitMaskCallisto` (`M_LitMaskCallisto`) are `S_Lit` and `S_LitMask` with the Callisto BRDF, from [The Rendering of The Callisto Protocol](https://advances.realtimerendering.com/s2023/SIGGRAPH2023-Advances-The-Rendering-of-The-Callisto-Protocol-JimenezPetersen.pdf) (Jimenez and Petersen, SIGGRAPH 2023 Advances, slides 70 to 111). It is a Lambert diffuse and GGX specular, which is what it is at its defaults, with controls on top that each change one thing, for matching a material to a reference. Besides their base shader's properties and maps, both have these:

| Property | Default | |
|:---------|:--------|:--|
| `DiffuseFresnel`, `DiffuseFresnelColor` | 1, white | brightens (above 1) or darkens (below 1) the diffuse at grazing angles, tinted by the color. For eyes and teeth, which look too dark from the side once they are right from the front. |
| `DiffuseFresnelFalloff`, `DiffuseFresnelTangentFalloff` | 0.75, 0.75 | how quickly towards grazing it builds up, and how much of it stays where the surface leans away from the light and the camera |
| `Retroreflection`, `RetroreflectionColor` | 1, white | brightens or darkens the light that comes back towards a light beside the camera, which flattens or rounds the shape. For skin, where it gives the green at the silhouette. |
| `RetroreflectionFalloff`, `RetroreflectionTangentFalloff` | 0.75, 0.75 | as for the diffuse Fresnel |
| `SmoothTerminator` | 0 | above 0 the light fades out before it reaches the terminator, the edge between lit and unlit, and below 0 it holds on longer. It leaves the retroreflection and grazing areas alone. |
| `SmoothTerminatorLength` | 0.5 | how far from the terminator that reaches |
| `SpecularFresnelFalloff` | 0.5 | Schlick's Fresnel at 0.5. Lower narrows the bright reflections at grazing angles and 0 removes them, higher widens them and 1 reflects everything as at a grazing angle. For backlit skin. |
| `DualSpecularOpacity`, `DualSpecularRoughnessScale` | 0, 2 | how much of a second highlight is used instead of the first, and its roughness as a multiple of the material's, for highlights one can't match. At 0 the second one costs nothing. |

The falloffs work alike: 0.5 is Schlick's exponent of 5, lower is a higher exponent and so a narrower effect, and 1 is an exponent of 0, which applies the effect everywhere.

- **Not as published:** the talk doesn't give the dual specular's math, only its two controls, so it blends two GGX highlights as Unreal's dual specular does. The talk's `Specular`, a dielectric's reflectance, is left out, and stays 4% as in the other lit shaders.
- **Ambient light** is shaded as in `S_Lit`, so a scene lit only by it shows none of the controls.
- **Presets:** `crimson/callisto_presets.hlsli` has the talk's values for ten measured materials as `CallistoParams`. Each one looks like its material only with the base color, roughness and metallic the talk fitted alongside it, which its comment gives.
- **A shader of your own** picks the Callisto BRDF by calling `ComputeLightingCallisto` with a `CallistoParams` instead of `ComputeLightingBurley`. Start one from `DefaultCallistoParams()` or a preset, or fill it in from the material's properties as `GetMaterialCallistoParams()` does in these two shaders.

### S_Unlit

`engine/shaders/S_Unlit.hlsl`, used by `M_Unlit`, draws `Color` times `ColorMap` with no lighting. Its alpha blends when the material is Transparent, and cuts out at `AlphaCutoff` when it is Cutout. `Tiling` multiplies the UVs.

### Debug views

- **`S_DebugNormals`** (`M_DebugNormals`) shows the world space normal as a color, `normal * 0.5 + 0.5`: +X is red, +Y is green and +Z is blue. Back faces get the normal flipped towards the camera, as a double-sided surface would be shaded.
- **`S_DebugUV`** (`M_DebugUV`) draws a checkerboard in UV space, 20 squares per UV unit times `Tiling`. Stretched squares mean distorted UVs, squares of different sizes on neighbouring surfaces mean the texture is spread thinner or packed denser there, and a pattern that jumps along a line marks a seam.

The debug colors are tone mapped like the rest of the scene, so they are close to, not exactly, the values they stand for.

The materials are `M_Default`, `M_LitMask`, `M_LitCallisto`, `M_LitMaskCallisto`, `M_Unlit`, `M_DebugNormals` and `M_DebugUV`.

### Cutout

A material whose render mode is **Cutout** has holes where its alpha is under its `AlphaCutoff`, for leaves, fences and chains. Unlike Transparent, it writes depth and needs no sorting. The engine only gives the shader the material's `AlphaCutoff` while its mode is Cutout, and 0 otherwise, so an opaque material never loses a pixel. A shader of your own can be cut out too, by declaring a `float AlphaCutoff` property and ending its pixel shader with `clip(alpha - AlphaCutoff)`. Put the `clip` last: a discarded pixel stops at once, and what comes after it in the pixels around it, such as the derivatives that pick a texture's mip level, would be wrong. For the holes to show in the shadows and the selection outline too, give it a `PSDepth` that clips the same way, as `S_Lit` does.

## Depth passes

The shadows and the selection outline only need to know where a mesh is, so they draw it with its material's **depth version**: the shader's `VSMain`, and its `PSDepth` if it has one. A mesh's shadow therefore has whatever shape its shader gives it, with nothing extra to write for a shader that moves its vertices.

```hlsl
// Optional. Takes what VSMain outputs, returns nothing, and is only there to discard.
void PSDepth(VertexOutput input)
{
    clip(Mask.Sample(LinearWrap, input.UV).a - AlphaCutoff);
}
```

- **Without a `PSDepth`** there is no pixel shader in those passes at all, and the depth comes straight from `VSMain`.
- **It can use the material's properties and textures,** like `PSMain`, and what only it uses is still part of the material.
- **It must not sample `ShadowAtlas`,** as the shadow pass is drawing into it.
- **Every material draws there as opaque,** with its own cull mode, so a transparent mesh still casts its shadow.

## Color and light

- **Colors in `.mat` files and lights are linear.** The scene renders in linear HDR and is tone mapped for the screen afterwards, so what a shader outputs is linear too. The Inspector's color swatches convert colors to show what they look like on screen.
- **A texture's `srgb` setting** is on by default, which is right for `ColorMap` and `EmissiveMap`. Turn it off for maps that hold data (normal, metallic-roughness, occlusion, mask and height). The Inspector warns when one of those slots has a texture with it on.
- **Light intensities are physical quantities**, as in glTF: illuminance for directional lights, and candela for point and spot lights. Light falls off with the square of the distance, and reaches zero at `Range`.
- **There is no image-based lighting yet** (#29), so `AmbientColor` counts as a uniform environment. Metals reflect that, so they look flat until there is an environment map.
