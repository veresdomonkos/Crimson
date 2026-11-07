# Contributing

How work on Crimson goes, from an issue to a merged pull request. How the engine is put together is in [docs/architecture.md](docs/architecture.md), writing shaders in [docs/shaders.md](docs/shaders.md), and building and testing in [docs/development.md](docs/development.md).

## Issues

Work starts from an [issue](https://github.com/veresdomonkos/Crimson/issues). Each one has four parts:
- **Problem:** what is wrong or missing, and why it matters
- **Scope:** what the change covers, and what it leaves out
- **Done when:** a checklist of what has to be true at the end
- **Where to start:** the files and functions to look at first

Labels sort them:

| Label | Values |
|:------|:-------|
| `type:` | `feature`, `improvement`, `bug`, `refactor`, `chore`, `docs` |
| `area:` | `renderer`, `assets`, `scene`, `editor`, `runtime`, `build` |
| `priority:` | `high` (do next: loses work, or blocks other tickets), `medium` (within the milestone), `low` (can move to a later milestone) |
| `size:` | `S` (up to a day), `M` (a few days), `L` (a week or two), `XL` (too big for one PR: split it when it starts) |

The [milestones](https://github.com/veresdomonkos/Crimson/milestones) say when an issue is due. A large piece of work gets an umbrella issue, split into issues of their own that are done one at a time.

## Branches

One branch per issue, made from the issue so GitHub links the two:

```bash
gh issue develop <number> --name feature/<short-name> --base main --checkout
```

The prefix says what kind of change it is: `feature/`, `fix/`, `chore/` or `docs/`.

## Commits

- **Subject:** lowercase, in the present tense, saying what the change does. For example, "keep a locked cursor where it was locked" or "cache the downloaded packages of the Linux CI jobs".
- **Body:** why the change was needed, and anything that isn't obvious from the code. The last commit of an issue ends with `Fixes #<number>`.
- **Size:** a commit should build on its own. A larger issue can be a few commits, one for each step.

## Pull requests

- **Description:**
  - what changed and why
  - what was checked, and what was left to CI
  - `Fixes #<number>`
- **CI:** it has to pass on all four jobs: Windows, Linux GCC, Linux with AddressSanitizer, and the Linux package.
- **Merging:** with a merge commit. Squash instead when the branch's history shouldn't land on `main`, for example when an early commit added files a later one moved to Git LFS.

## Code

C++23, in the style the code already has:

- **Formatting:** 4 spaces, braces on their own line.
- **Names:**
  - `PascalCase` for types, functions and public members
  - `m_camelCase` for private members, `s_camelCase` for statics, `kPascalCase` for constants
  - the engine in `crimson::`, the editor in `crimson::editor::`, and its panels in `crimson::editor::ui::`
- **Files:** headers in `include/` (`crimson/...` and `crimson_editor/...`), sources in `src/`. Paths are `snake_case`.
- **Comments say why,** or what isn't plain from the code, as a short sentence. Not what the next line does.
- **No cost for testability:** nothing gets a virtual function, or anything else that costs per frame, only so a test can fake it. See [Testing](docs/development.md#testing).
- **GPU resources** only change between frames. Panels queue such changes in `EditorContext::Deferred`.
- **Editor UI:**
  - Colors come from `crimson_editor/ui/editor_theme.hpp`, never written out in a panel.
  - Icons come from `crimson_editor/ui/icons.hpp`.
  - Inspector fields go through `crimson_editor/ui/property_fields.hpp`.
- **Errors** are logged with `LOG_ERROR`. That puts them in the Console and in the corner of the window, and makes a test fail unless it expected them.

### Engine assets

The engine's own assets get a prefix for their kind: `S_` for shaders, `M_` for materials, `SM_` for meshes, `T_` for textures, then a `PascalCase` name, such as `S_Lit.hlsl`. Shader include files (`crimson/*.hlsli`) don't, as shaders include them by path. A rename goes in `kRenamedEngineAssets` in `engine/src/asset_manager/asset_manager.cpp`, so nothing saved with the old name breaks.

## Tests

A change comes with tests for what it does:
- **`tests/engine/` and `tests/editor/`** for anything that can run without a GPU, against `tests/support/fake_gpu.hpp` when it needs GPU resources
- **`tests/gpu/`** for what only shows on a real backend, such as what ends up in the pixels

What each covers is in [docs/development.md](docs/development.md#testing). CI runs them all on every pull request.

## Docs

- **Behavior changes:** a change that alters what the editor does, or how the engine is used, updates the page it belongs to:
  - the [README](README.md), for what users see
  - [architecture](docs/architecture.md), [shaders](docs/shaders.md) or [development](docs/development.md), for how it works
- **Screenshots** come from `tools/capture_screenshots.ps1`.
- **New third-party code or assets** go in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), with their license.
