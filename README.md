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
- **Scene cameras:** perspective or orthographic cameras that look along their -Z axis, one of them the scene's main camera, which the Scene view can look through.
- **Built-in shaders:** lit with glTF or mask-map packing, with parallax or triplanar and biplanar projection, unlit, and debug views of normals and UVs.
- **Shaders that reload as you save:** HLSL, compiled inside the editor for both backends, with the material's properties and textures read from the shader itself.
- **Scenes inside scenes:** any scene can be placed in another one as an instance. That's how reusable pieces are made, without a separate prefab type.
- **Built-in meshes:** a cube, sphere, cylinder, capsule, plane and quad, from **Create 3D Object**.
- **An editor that stays out of the way:**
  - Unity-style camera controls and gizmos
  - Hierarchy search
  - undo and redo for every change to the scene and its materials
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

1. **Open a project.** Start the editor and open `samples/Demo`, or make a new project, which is a folder with an `assets/` folder in it. `--project <folder>` opens one straight away. The editor uses Vulkan, and OpenGL if Vulkan can't start; `--opengl` picks OpenGL.
2. **Add something to the scene.** Right-click in the Hierarchy, or use the **Create** menu, for an empty entity, a light, or a built-in mesh under **Create 3D Object**. Meshes can also be dragged from the Assets panel into the Scene view.
3. **Give it a material.** Right-click in the Assets panel for **New Material**, pick it with the picker next to the Mesh Renderer's material, or drop it on the mesh in the Scene view.
4. **Write a shader.** **New Shader** in the Assets panel starts a lit one. Double-click it to open it in the code editor set in **Edit > Preferences**, and it reloads every time it's saved. [docs/shaders.md](docs/shaders.md) has what a shader can use.
5. **Save.** **File > Save All** or `Ctrl+S` saves the scene and the materials. Closing with unsaved changes asks first. If the editor closes without saving, opening the scene again offers its unsaved changes.

| Panel | What it's for |
|:------|:--------------|
| **Scene** | the scene, with a gizmo on the selection. The camera button in its toolbar looks through the selected camera. |
| **Hierarchy** | the scene's entities. Drag to reparent, `F2` or double-click to rename, `Delete` to delete, and search at the top. |
| **Inspector** | the selected entity or asset. Right-click any field to reset it. Pick assets with the button next to a field, or drop them on it. |
| **Assets** | the project's files. Drag them into the scene or the Inspector, and right-click for more. **Show Engine Content** adds the engine's built-in ones under an Engine root of their own, named with a prefix for their kind: `S_Lit.hlsl`, `M_Default.mat`, `SM_Cube.obj`. |
| **Console** | everything the engine logs |
| **Scene Settings** | ambient light, the main camera and post-processing (exposure and tone mapping), saved with the scene |
| **Project Settings** | settings saved with the project and shared by everyone on it, for now the main scene, the one a built game starts with. Opened from **Edit > Project Settings**. |
| **Stats** | frame times, draw calls and triangles |

Every panel but Scene can be closed from its tab and opened again from **View**, which also has **Reset Layout**.

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
| `W` / `E` / `R` | Gizmo: move / rotate / scale, while the Scene panel has focus |
| `Delete` | Delete the selected entity, while the Scene or the Hierarchy panel has focus |
| `Left Ctrl` while dragging the gizmo | Snap to 0.5 units, 15 degrees or 0.1 scale |
| `F2` | Rename the selected entity, in the Hierarchy |
| `Ctrl+S` | Save everything |
| `Ctrl+Z` | Undo the last change to the scene or a material |
| `Ctrl+Y` or `Ctrl+Shift+Z` | Redo |

## Roadmap

The work is planned in [milestones](https://github.com/veresdomonkos/Crimson/milestones):

- **v0.1, First game:** the first release, an engine you can make a game with: import real models, build a scene, write gameplay code, play it in the editor and ship it as a standalone player.
- **v0.2, Gameplay systems:** physics, audio, animation and game UI, the systems a game needs beyond moving things around.
- **v0.3, Authoring:** building content is comfortable: scene editing, instances that can differ from their source, and an asset workflow that reloads, previews and knows what uses what.
- **v0.4, Rendering quality:** lighting and shadows hold up on a large level such as Sponza.
- **v0.5, Speed and polish:** the editor stays responsive while loading and compiling, and looks and feels like a finished tool.

Each milestone is split into epics, issues labeled `type: epic` whose parts are their sub-issues. Issues that have to come first are marked as blocking the ones that need them.

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) is how work goes, from an issue to a merged pull request. How it all works is in the docs:

- [architecture](docs/architecture.md): the engine and the editor
- [shaders](docs/shaders.md): writing shaders
- [development](docs/development.md): building, testing, releasing and profiling
- [deliver](docs/deliver.md): taking an issue to a pull request with Claude Code's `/crimson-deliver`

## License

Crimson is under the [MIT License](LICENSE). The fonts it ships with have their own licenses, in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
