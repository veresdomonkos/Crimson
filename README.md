# Crimson Engine

Crimson is a small real-time 3D engine in C++23, with an editor. It renders with **Vulkan 1.3** or **OpenGL 4.5** from the same HLSL shaders, in linear HDR with physically based materials, lights and shadows. **Crimson Editor** is where scenes are put together, materials and shaders are made, and assets are kept, in a dockable layout built on Dear ImGui.

<p align="center">
  <img src="docs/images/editor-overview.png" alt="Crimson Editor with the demo scene, the bunny selected" width="900">
</p>

<table>
  <tr>
    <td align="center" valign="top"><img src="docs/images/editor-material.png" alt="A material in the Inspector" height="300"><br>Materials show what their shader declares</td>
    <td align="center" valign="top"><img src="docs/images/editor-assets.png" alt="The Assets panel" height="300"><br>The project's assets</td>
    <td align="center" valign="top"><img src="docs/images/project-manager.png" alt="The project manager" height="300"><br>Projects</td>
  </tr>
</table>

## Highlights

- **Physically based rendering:** metallic-roughness materials with the same slots as glTF, rendered in linear HDR and tone mapped for the screen.
- **Lights and shadows:** any number of directional, point and spot lights, each of which can cast a shadow.
- **Shaders that reload as you save:** HLSL, compiled inside the editor for both backends, with the material's properties and textures read from the shader itself.
- **Scenes inside scenes:** any scene can be placed in another one as an instance. That's how reusable pieces are made, without a separate prefab type.
- **Built-in meshes:** a cube, sphere, cylinder, capsule, plane and quad, from **Create 3D Object**.
- **An editor that stays out of the way:**
  - Unity-style camera controls and gizmos
  - Hierarchy search
  - an Inspector where every field resets to its default
  - errors and saves shown in the corner, so nothing goes unnoticed
- **Your own tools:** shaders open in VS Code, Visual Studio, Rider or CLion, and other files in their usual program, or in Explorer.
- **Two backends, one engine:** Vulkan and OpenGL behind the same interfaces, both tested on every change.

## Download

Builds of each release are on the [Releases page](https://github.com/veresdomonkos/Crimson/releases), with the demo project.

- **Windows** needs 10 or newer and a graphics driver, as `vulkan-1.dll` comes with the driver. It needs that file to start even with `--opengl`.
- **Linux** is one AppImage, built on Ubuntu 24.04 so it runs on that or newer.
  - Make it executable with `chmod +x`.
  - If it complains about FUSE, install `libfuse2t64` (`libfuse2` on older distros).
  - It needs a graphics driver with the Vulkan loader (`libvulkan1`).
  - It runs on X11, or on Wayland through XWayland.

## Building from source

```bash
git clone --recursive https://github.com/veresdomonkos/Crimson.git
cd Crimson
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Editor
```

It needs a C++23 compiler, CMake 3.22+, the [Vulkan SDK](https://vulkan.lunarg.com/) and Git LFS. [docs/development.md](docs/development.md) has the details, including which MinGW to use on Windows, the options, and running the tests.

## First steps

1. **Open a project.** Start the editor and open `samples/Demo`, or make a new project, which is a folder with an `assets/` folder in it. `--project <folder>` opens one straight away, and `--vulkan` or `--opengl` picks the backend.
2. **Add something to the scene.** Right-click in the Hierarchy, or use the **Create** menu, for an empty entity, a light, or a built-in mesh under **Create 3D Object**. Meshes can also be dragged from the Assets panel into the Scene view.
3. **Give it a material.** Right-click in the Assets panel for **New Material**, pick it in the Inspector's Mesh Renderer, or drop it on the mesh in the Scene view.
4. **Write a shader.** **New Shader** in the Assets panel starts a lit one. Double-click it to open it in the code editor set in **Edit > Preferences**, and it reloads every time it's saved. [docs/shaders.md](docs/shaders.md) has what a shader can use.
5. **Save.** **File > Save All** or `Ctrl+S` saves the scene and the materials. Closing with unsaved changes asks first.

| Panel | What it's for |
|:------|:--------------|
| **Scene** | the scene, with a gizmo on the selection |
| **Hierarchy** | the scene's entities. Drag to reparent, `F2` or double-click to rename, `Delete` to delete, and search at the top. |
| **Inspector** | the selected entity or asset. Right-click any field to reset it. |
| **Assets** | the project's files. Drag them into the scene or the Inspector, and right-click for more. **Show Engine Content** adds the engine's built-in ones under an Engine root of their own, named with a prefix for their kind: `S_Lit.hlsl`, `M_Default.mat`, `SM_Cube.obj`. |
| **Console** | everything the engine logs |
| **Render Settings** | exposure, tone mapping and ambient light, saved with the scene |
| **Stats** | frame times, draw calls, triangles and the shadow views drawn and kept |

## Editor controls

Based on Unity's scene view. Mouse controls start over the Scene panel, and the move keys fly the camera while the right mouse button is held.

| Input | Action |
|:------|:-------|
| Right mouse (hold) + `W` `A` `S` `D` or arrow keys | Move where the camera looks |
| Right mouse (hold) + `Q` / `E` | Down / up |
| `Shift` | Move 3 times faster |
| Right mouse (hold) | Look around |
| Scroll while holding the right mouse button | Change the move speed |
| Middle mouse (hold) | Pan |
| Scroll | Zoom |
| `Alt` + left mouse (hold) | Orbit |
| `Alt` + right mouse (hold) | Zoom |
| `F` | Frame the selected entity |
| Left click | Select the nearest mesh under the mouse by its bounds, or clear the selection. The selection gets an outline. |
| `W` / `E` / `R` | Gizmo: move / rotate / scale |
| `Delete` | Delete the selected entity, while the Scene panel has focus |
| `Left Ctrl` while dragging the gizmo | Snap to 0.5 units, 15 degrees or 0.1 scale |
| `F2` | Rename the selected entity, in the Hierarchy |
| `Ctrl+S` | Save everything |

## Roadmap

The work is planned in [milestones](https://github.com/veresdomonkos/Crimson/milestones):

- **v0.1, Safe scene editing:** editing a scene can't lose work, and the first tagged release is out.
- **v0.2, Assets and instances:** real content comes in without hand-written files, and instances can differ from their source.
- **v0.3, Rendering quality:** lighting and shadows hold up on a large level such as Sponza.
- **v0.4, Running a scene:** a scene can run, in the editor first, then as a game outside of it.

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) is how work goes, from an issue to a merged pull request. How it all works is in the docs:

- [architecture](docs/architecture.md): the engine and the editor
- [shaders](docs/shaders.md): writing shaders
- [development](docs/development.md): building, testing, releasing and profiling

## License

Crimson is under the [MIT License](LICENSE). The fonts it ships with have their own licenses, in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
