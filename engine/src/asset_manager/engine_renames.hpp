#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace crimson
{
    using EngineRename = std::pair<std::string_view, std::string_view>; // old path, new path

    // Engine assets that were renamed. What a project saved with an old path, or the id made from it, still finds the
    // asset, and opening the project writes the new one into its files. A rename only goes at the end, also for an
    // asset renamed before, and isn't needed for one that was never in a release.
    inline constexpr std::array<EngineRename, 12> kRenamedEngineAssets = { {
        { "engine/shaders/main.hlsl",          "engine/shaders/S_Lit.hlsl" },
        { "engine/materials/default.mat",      "engine/materials/M_Default.mat" },
        { "engine/meshes/cube.obj",            "engine/meshes/SM_Cube.obj" },
        { "engine/meshes/sphere.obj",          "engine/meshes/SM_Sphere.obj" },
        { "engine/meshes/cylinder.obj",        "engine/meshes/SM_Cylinder.obj" },
        { "engine/meshes/capsule.obj",         "engine/meshes/SM_Capsule.obj" },
        { "engine/meshes/plane.obj",           "engine/meshes/SM_Plane.obj" },
        { "engine/meshes/quad.obj",            "engine/meshes/SM_Quad.obj" },
        { "engine/shaders/tonemap.hlsl",       "engine/shaders/internal/S_ToneMap.hlsl" },
        { "engine/shaders/outline.hlsl",       "engine/shaders/internal/S_Outline.hlsl" },
        { "engine/materials/tonemap.mat",      "engine/materials/internal/M_ToneMap.mat" },
        { "engine/materials/outline.mat",      "engine/materials/internal/M_Outline.mat" },
    } };

    // Where a path ends up through the renames, for an asset renamed more than once. Empty if that doesn't exist, which a
    // loop in the renames also gives.
    template <typename Exists>
    [[nodiscard]] std::string_view FollowRenames(std::span<const EngineRename> renames, std::string_view path, Exists exists)
    {
        for (size_t steps = 0; steps <= renames.size(); ++steps)
        {
            if (exists(path))
                return path;

            const auto next = std::ranges::find(renames, path, &EngineRename::first);
            if (next == renames.end())
                return {};

            path = next->second;
        }

        return {};
    }
}
