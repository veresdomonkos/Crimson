# Writing shaders

Shaders are HLSL, with a `VSMain` and a `PSMain` in one `.hlsl` file. The same file runs on both backends:

```
.hlsl -> glslang -> SPIR-V -> Vulkan
                           -> SPIRV-Cross -> GLSL 4.5 -> OpenGL
```

glslang is linked into the engine, so shaders compile in the editor as they are saved. Its HLSL support is around Shader Model 5, so Shader Model 6 features such as wave ops and mesh shaders aren't available.

## Making one

**New Shader** in the Assets panel's right-click menu makes a lit shader to start from, with a color, metallic and roughness, and a color map. Give a material that shader, and the material's Inspector shows the properties and textures the shader declares.

- **Hot reload:** a project's shader reloads within half a second of being saved, along with its materials. If the new version doesn't compile, the error shows in the corner of the window and in the Console, and the last version that compiled stays.
- **Opening it:** double-click it in the Assets panel. That opens it in the **External Script Editor** set in **Edit > Preferences**: VS Code, Visual Studio, Rider, CLion or any program. VS Code opens the project as its workspace, so the [HLSL Tools](https://marketplace.visualstudio.com/items?itemName=TimGJones.hlsltools) extension finds the engine's headers through the project's `shadertoolsconfig.json`.
- **Engine shaders** are built into the editor, so changing one means rebuilding it.

## What a shader declares

Resources are grouped by how often they change:

| Space | Scope | Resources |
|:------|:------|:----------|
| `space0` | per frame | `FrameBlock` (`AmbientColor`, `Time` and `DeltaTime` in seconds), `Lights`, `Shadows`, `Objects`, `ShadowAtlas` and the samplers `LinearWrap`, `LinearClamp`, `PointClamp`, `ShadowCompare` |
| `space1` | per pass | `ViewBlock` (`ViewProj`, `CameraPosition`) |
| `space2` | per material, **optional** | one constant buffer (`b0`) and any textures (`t0` and up), both looked up by name |
| push constant | per draw | `DrawData` (`ObjectIndex`) |

`crimson/bindings.hlsli` declares the standard ones, so shaders include it instead of restating them, and a material shader declares only its own:

```hlsl
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color; // @default 1 1 1 1
    float Roughness; // @default 0.5 @range 0 1
};

Texture2D Albedo : register(t0, space2);
Texture2D Bumps : register(t1, space2); // @default normal

// VSMain uses GetObjectData() and ViewProj, PSMain fills in a Surface for ComputeLighting() and uses Albedo.Sample(LinearWrap, uv)
```

- **Discovery:** material properties and textures are found with SPIRV-Reflect, so the shader is the only place they're declared. The standard blocks are checked against the engine's structs.
- **Samplers:** materials share the samplers in `space0`, and can't declare their own.
- **Import errors:** a shader fails to import, with an error in the console, if it uses an unknown space or a standard resource whose type or size doesn't match the engine.
- **Vertex inputs:** meshes give `POSITION` (float3), `NORMAL` (float3), `TEXCOORD0` (float2) and `TANGENT` (float4: xyz along +U, w the side the bitangent is on, ±1, and 0 for a mesh without UVs). There are no vertex colors. Inputs take their locations from their order in the input struct, so declare them in this order; a shader can leave out the ones at the end.

### Annotations

The comment after a declaration says what materials start with:

| Annotation | On | Does |
|:-----------|:---|:-----|
| `@default 1 0.5 0` | a property | the value a material has until it sets its own. Without one, a property starts at zero. |
| `@range 0 1` | a single number | a slider in the Inspector, between those values |
| `@default normal` | a texture | an empty slot samples a flat normal. Without it, an empty slot samples white. |

A material property that's back at its `@default` isn't written to the `.mat` file, and **Reset to default** in the Inspector puts it there.

## The headers

Includes resolve relative to the shader's folder, then to the engine's shaders.

| Header | Has |
|:-------|:----|
| `crimson/bindings.hlsli` | the standard resources above, and `GetObjectData()` for the current draw |
| `crimson/math.hlsli` | `PI`, `TAU`, `HALF_PI`, `INV_PI`, `E` and `EPSILON` |
| `crimson/transforms.hlsli` | `ObjectToWorldPosition`, `ObjectToWorldDirection`, `ObjectToWorldNormal`, `WorldToObjectPosition`, `WorldToObjectDirection`, `WorldToClip`, `ObjectToClip` and `ObjectWorldPosition()` |
| `crimson/lighting.hlsli` | `Surface` and `ComputeLighting(Surface)`, the scene's lights and shadows for a point on a surface. Includes the three above. |

## The default lit shader

`engine/shaders/S_Lit.hlsl` is the default lit shader: metallic-roughness PBR with a Cook-Torrance GGX specular and a Lambert diffuse, with the same material slots as glTF.

| Property | Default | |
|:---------|:--------|:--|
| `Color` | white | linear, multiplied with `ColorMap` |
| `Metallic`, `Roughness` | 0, 0.5 | multiplied with `MetallicRoughnessMap` (blue and green), as in glTF |
| `NormalStrength` | 1 | scales `NormalMap`, with green pointing up in the image (the OpenGL convention, which glTF uses too) |
| `OcclusionStrength` | 1 | how much `OcclusionMap` (red) darkens ambient light |
| `EmissiveColor`, `EmissiveStrength` | black, 1 | linear, multiplied with `EmissiveMap` |
| `AlphaCutoff` | 0.5 | for a cutout material, where the alpha of `Color` times `ColorMap` is under it there is nothing |

Normal maps use the mesh's tangents, which the importer works out with MikkTSpace, the same way Blender, Substance and other baking tools do, so baked normal maps match. A mesh without UVs has no tangents to go by and stays flat.

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
- **A texture's `srgb` setting** is on by default, which is right for `ColorMap` and `EmissiveMap`. Turn it off for maps that hold data (normal, metallic-roughness, occlusion). The Inspector warns when one of those slots has a texture with it on.
- **Light intensities are physical quantities**, as in glTF: illuminance for directional lights, and candela for point and spot lights. Light falls off with the square of the distance, and reaches zero at `Range`.
- **There is no image-based lighting yet** (#29), so `AmbientColor` counts as a uniform environment. Metals reflect that, so they look flat until there is an environment map.
