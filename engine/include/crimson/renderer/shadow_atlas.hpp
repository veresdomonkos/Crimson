#pragma once
#include <algorithm>
#include <array>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "crimson/renderer/frame_gpu_data.hpp"

namespace crimson
{
    struct ShadowAtlasSettings
    {
        uint32_t AtlasSize = 4096;
        uint32_t TileSize = 1024;

        // Directional lights have no position, they cover this area of the world
        glm::vec3 DirectionalCenter{0.0f};
        float DirectionalRadius = 8.0f;
        float DirectionalDistance = 30.0f;

        float DirectionalDepthBias = 0.0015f;
        float PerspectiveDepthBias = 0.0004f;
        float NormalBias = 0.05f;
    };

    // Assigns atlas tiles to the lights with CastsShadow set and fills views with one view per tile.
    // A directional or spot light takes one tile, a point light six, in the order +X -X +Y -Y +Z -Z.
    // Returns how many lights didn't fit in the atlas, those don't cast shadows.
    inline uint32_t BuildShadowViews(std::vector<Light>& lights, const ShadowAtlasSettings& settings, std::vector<ShadowView>& views)
    {
        const uint32_t tilesPerRow = settings.AtlasSize / settings.TileSize;
        const uint32_t room = tilesPerRow * tilesPerRow;

        uint32_t skipped = 0;
        views.clear();

        const auto addView = [&](const glm::mat4& viewProj, const glm::vec3& eye, float depthBias)
        {
            const uint32_t index = static_cast<uint32_t>(views.size());
            const uint32_t x = index % tilesPerRow * settings.TileSize;
            const uint32_t y = index / tilesPerRow * settings.TileSize;
            const float atlasSize = static_cast<float>(settings.AtlasSize);

            ShadowView view{};
            view.Shadow.ViewProj = viewProj;
            view.Shadow.AtlasRect = glm::vec4(x, y, settings.TileSize, settings.TileSize) / atlasSize;
            view.Shadow.DepthBias = depthBias;
            view.Shadow.NormalBias = settings.NormalBias;
            view.Shadow.TexelSize = 1.0f / atlasSize;
            view.Viewport = RenderRect{ static_cast<int32_t>(x), static_cast<int32_t>(y), settings.TileSize, settings.TileSize };
            view.Eye = eye;
            views.push_back(view);
        };

        const auto stableUp = [](const glm::vec3& direction)
        {
            return glm::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        };

        for (Light& light : lights)
        {
            light.ShadowIndex = -1;

            if (!light.CastsShadow)
                continue;

            const uint32_t needed = light.Type == LightType::Point ? 6 : 1;
            if (views.size() + needed > room)
            {
                ++skipped;
                continue;
            }

            light.ShadowIndex = static_cast<int32_t>(views.size());

            switch (light.Type)
            {
                case LightType::Directional:
                {
                    const glm::vec3 direction = glm::normalize(light.Direction);
                    const glm::vec3 eye = settings.DirectionalCenter - direction * settings.DirectionalDistance;
                    const float radius = settings.DirectionalRadius;
                    const glm::mat4 view = glm::lookAt(eye, settings.DirectionalCenter, stableUp(direction));
                    const glm::mat4 projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.1f, settings.DirectionalDistance * 2.0f);
                    addView(projection * view, eye, settings.DirectionalDepthBias);
                    break;
                }
                case LightType::Spot:
                {
                    const glm::vec3 direction = glm::normalize(light.Direction);
                    const glm::mat4 view = glm::lookAt(light.Position, light.Position + direction, stableUp(direction));
                    const glm::mat4 projection = glm::perspectiveRH_ZO(light.OuterConeAngle * 2.0f, 1.0f, 0.1f, light.Range);
                    addView(projection * view, light.Position, settings.PerspectiveDepthBias);
                    break;
                }
                case LightType::Point:
                {
                    constexpr std::array<glm::vec3, 6> directions = {
                        glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)
                    };

                    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.1f, light.Range);

                    for (const glm::vec3& direction : directions)
                        addView(projection * glm::lookAt(light.Position, light.Position + direction, stableUp(direction)), light.Position, settings.PerspectiveDepthBias);

                    break;
                }
            }
        }

        return skipped;
    }
}
