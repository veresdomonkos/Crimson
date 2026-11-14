# Building, testing and releasing

## Requirements

- A C++23 compiler: GCC 13+, Clang 16+ or MSVC 19.35+
- CMake 3.22+
- The [Vulkan SDK](https://vulkan.lunarg.com/) 1.3+, for the headers, the loader and the validation layers
- A GPU and driver with Vulkan 1.3 (dynamic rendering, synchronization2) and/or OpenGL 4.5
- [Git LFS](https://git-lfs.com/), for the fonts, the icons, the meshes and the sample project
- Optionally [ccache](https://ccache.dev/) 4.8+, which makes rebuilding files that were built before almost free. CMake uses it when it is on the PATH. It is `pacman -S mingw-w64-ucrt-x86_64-ccache` in MSYS2 and `apt install ccache` on Ubuntu.
- Optionally [lld](https://lld.llvm.org/), which links the large Debug builds many times faster than GNU ld. CMake uses it when the compiler can. It is `pacman -S mingw-w64-ucrt-x86_64-lld` in MSYS2 and `apt install lld` on Ubuntu.

### Windows

The editor is developed with MinGW. **Use the one from [MSYS2](https://www.msys2.org/)'s UCRT64 environment** (`pacman -S mingw-w64-ucrt-x86_64-gcc`).

The MinGW that CLion 2026.2 bundles can't link the editor. Its static `libwinpthread` is built for UCRT while the rest of it uses MSVCRT, so linking fails with an undefined reference to `__intrinsic_setjmpex`. In CLion, add a MinGW toolchain at the `ucrt64` folder of MSYS2, keep the bundled CMake and Ninja, and use it for the CMake profiles.

## Building

```bash
git clone --recursive https://github.com/veresdomonkos/Crimson.git
cd Crimson
git lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Editor
```

- **Debug info:** the libraries in `external/` only get line tables, so call stacks through them still have files and lines, but their variables can't be inspected. Our own code gets full debug info.
- **Static linking:** the editor links everything statically, including the shader compiler, so it needs no DLLs of its own. It only uses the system's graphics driver, and the executable can run from anywhere.
- **Measuring performance:** use **Release** (or RelWithDebInfo). Debug builds, especially with validation layers enabled, are many times slower and give misleading numbers. The Stats panel says when it is a Debug build.
- **Choosing a backend:** `--vulkan` or `--opengl` on the command line picks the backend, and `--project <folder>` opens a project straight away.

| Setting | Does |
|:--------|:-----|
| `-DCRIMSON_BUILD_TESTS=OFF` | leaves the tests out |
| `-DCRIMSON_ENABLE_PROFILER=OFF` | compiles the Tracy instrumentation out |
| `-DCRIMSON_USE_CCACHE=OFF` | builds without ccache, even when it is installed |
| `-DCRIMSON_USE_LLD=OFF` | links with the compiler's default linker, even when lld is installed |
| `CRIMSON_VSYNC=1` | limits the editor to the display's refresh rate. It renders as fast as it can otherwise. |
| `CRIMSON_VK_SYNC_VALIDATION=1` | adds synchronization validation to the Vulkan validation layers, which Debug builds turn on when they are installed |

## Git hooks

Configuring CMake sets `core.hooksPath` to [`.githooks`](../.githooks), unless the checkout already has hooks of its own set. Without CMake, `git config core.hooksPath .githooks` does the same. The hooks refuse a commit that breaks the [conventions](../CONTRIBUTING.md):

- **pre-commit:** a commit on `main`, a branch named outside them, and staged lines with trailing whitespace, a CR or tab indentation (in C++, HLSL and CMake files, set in [`.gitattributes`](../.gitattributes)). It also refuses a changed source or text file that doesn't end with a newline. Only the lines and files a commit changes are checked, so older code is left alone until it is edited.
- **commit-msg:** an empty subject, one that ends with a period or is over 100 characters, and a body without a blank line before it.

The rules are in [`tools/check_conventions.sh`](../tools/check_conventions.sh), which the `conventions` CI job runs too, so `--no-verify` only moves the failure to the pull request. It can also run by hand:

```bash
sh tools/check_conventions.sh branch "$(git branch --show-current)"
sh tools/check_conventions.sh files --cached
sh tools/check_conventions.sh files main...HEAD
```

## Testing

```bash
cmake --build build --target CrimsonTests CrimsonGpuTests
ctest --test-dir build --output-on-failure
```

Tests use [Catch2](https://github.com/catchorg/Catch2). **A run fails on any logged error that a test didn't expect**, and in Debug builds that includes validation errors.

### `CrimsonTests`

This needs no GPU or window. It covers everything above the graphics APIs:

- **Scene:** hierarchy, transforms and raycasts.
- **Assets and projects:** scanning, metas, loading and its failures, shader reloading, and the built-in meshes, using real files in a temporary folder (`tests/support/temp_folder.hpp`).
- **Shaders:** every engine shader goes through what the backends do before reaching the GPU: HLSL to SPIR-V, reflection, and translation to GLSL.
- **Frames:** pass recording, draw sorting, shadow views, the frame clock and `SceneRenderer`.
- **The editor's logic:** asset edits, texture previews, recent projects, settings, the script editor's arguments, and file URIs.

Code talks to the GPU through `GpuResourceManager` and `Renderer`, which are already interfaces, so `tests/support/fake_gpu.hpp` stands in for them. Testability shouldn't cost anything per frame, so nothing gains a virtual function just to be faked: `BuildDrawOrder` takes its materials as a template parameter, and `Scene`, `Entity` and `Input` stay concrete.

### `CrimsonGpuTests`

This runs the real backends, once with `--backend gl` and once with `--backend vk`, and opens a window to do it. `ctest --label-exclude gpu` skips it.

- **Rendering:** renders into a target and checks the pixels with `ReadColorPixels`. It covers placement, depth, materials, textures, blending, lights, all three kinds of shadow, outlines, shader time and the space helpers.
- **The editor:** drives the real `EditorApplication` frame by frame. It covers the project manager, opening and closing projects, projects with problems, and shaders reloading while a material is selected. The panels draw, but nothing clicks on them. It also opens the asset picker, in both views, and pings an asset.
- **Input:** a locked cursor staying in place.

## CI

[CI](../.github/workflows/ci.yml) runs on pull requests and on `main`, and a new push cancels the run it replaces.

| Job | Runs on | Does |
|:----|:--------|:-----|
| `conventions` | Ubuntu 24.04 | checks the branch name, commit messages and whitespace, like the [git hooks](#git-hooks). Only on pull requests. |
| `windows` | Windows, MinGW | builds Release, runs `CrimsonTests`, packages a zip |
| `linux-package` | Ubuntu 24.04 | packages an AppImage, and checks that it starts |
| `linux-gcc` | Ubuntu 26.04 | builds Debug, runs both test programs |
| `linux-asan` | Ubuntu 26.04 | the same, with AddressSanitizer |

- **No GPU on Linux:** Mesa renders on the CPU, with llvmpipe for OpenGL and lavapipe for Vulkan, on an Xvfb display. A bug in a GPU's driver won't show up there, so `CrimsonGpuTests` is still worth running locally.
- **Caches:** ccache, the Vulkan SDK, the Git LFS files and the downloaded Ubuntu packages are kept between runs. The Ubuntu mirror can be slow, so the package install stops after 15 minutes. Running the job again goes on from what is in the cache.

## Releases

Pushing a version tag starts a release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

- [The workflow](../.github/workflows/release.yml) checks that the tag is on `main`, and runs all of CI.
- It then creates a draft on the Releases page, with the Windows zip, the Linux AppImage and the demo project.
- A draft isn't public, so it can be downloaded and checked before publishing it by hand.
- Every CI run also has the same packages to download from its page.

## Profiling

Crimson is instrumented with [Tracy](https://github.com/wolfpld/tracy): CPU zones, per-pass GPU zones on both backends, and a `Draw Calls` plot. To profile, run the editor and connect the [Tracy v0.14.1 viewer](https://github.com/wolfpld/tracy/releases/tag/v0.14.1) to `127.0.0.1`. The viewer version must match the `external/tracy` submodule.

Data is only collected while a viewer is connected, and the client only listens on localhost. Add zones with the macros in `crimson/core/profiler.hpp`.

## Screenshots

The screenshots in `docs/images/` are taken from the running editor:

```powershell
pwsh tools/capture_screenshots.ps1
```

- It runs on Windows, with the editor built in Release.
- It opens the editor on a copy of `samples/Demo`, clicks through it, and saves the images. Leave the mouse and keyboard alone while it does.
- Your layout, recent projects and settings are put back afterwards.
- The clicks go where the panels are in the default layout. If the layout changes, move them in the script.
