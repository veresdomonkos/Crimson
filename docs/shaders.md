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
- **Vertex inputs:** meshes give `POSITION` (float3), `NORMAL` (float3) and `TEXCOORD0` (float2), with no tangents and no vertex colors. Inputs take their locations from their order in the input struct.

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

`engine/shaders/main.hlsl` is the default lit shader: metallic-roughness PBR with a Cook-Torrance GGX specular and a Lambert diffuse, with the same material slots as glTF.

| Property | Default | |
|:---------|:--------|:--|
| `Color` | white | linear, multiplied with `ColorMap` |
| `Metallic`, `Roughness` | 0, 0.5 | multiplied with `MetallicRoughnessMap` (blue and green), as in glTF |
| `NormalStrength` | 1 | scales `NormalMap`, with green pointing up in the image (the OpenGL convention, which glTF uses too) |
| `OcclusionStrength` | 1 | how much `OcclusionMap` (red) darkens ambient light |
| `EmissiveColor`, `EmissiveStrength` | black, 1 | linear, multiplied with `EmissiveMap` |

Normal maps don't need tangents in the mesh, as the shader works out the directions from how position and UV change between pixels. A mesh without UVs stays flat.

## Color and light

- **Colors in `.mat` files and lights are linear.** The scene renders in linear HDR and is tone mapped for the screen afterwards, so what a shader outputs is linear too. The Inspector's color swatches convert colors to show what they look like on screen.
- **A texture's `srgb` setting** is on by default, which is right for `ColorMap` and `EmissiveMap`. Turn it off for maps that hold data (normal, metallic-roughness, occlusion). The Inspector warns when one of those slots has a texture with it on.
- **Light intensities are physical quantities**, as in glTF: illuminance for directional lights, and candela for point and spot lights. Light falls off with the square of the distance, and reaches zero at `Range`.
- **There is no image-based lighting yet** (#29), so `AmbientColor` counts as a uniform environment. Metals reflect that, so they look flat until there is an environment map.
