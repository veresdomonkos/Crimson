#pragma once
#include "glm/glm.hpp"

namespace crimson
{
    enum class LightType : glm::uint32_t
    {
        Directional = 0,
        Point       = 1,
        Spot        = 2
    };

    struct GPULight
    {
        glm::vec4 PositionAndType;   // xyz = position, w = type (uint bitpattern)
        glm::vec4 DirectionAndRange; // xyz = direction, w = range
        glm::vec4 ColorAndIntensity; // rgb = color, a = intensity
        glm::vec4 SpotAngles;        // x = cos(inner), y = cos(outer), z,w = reserved
    };

    struct Light
    {
        LightType Type = LightType::Directional;
        glm::vec3 Position{0.0f};
        glm::vec3 Direction{0.0f, -1.0f, 0.0f};
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
        float Range = 10.0f;
        float InnerConeAngle = 0.0f;
        float OuterConeAngle = 0.785398f; // 45 degrees

        [[nodiscard]] GPULight ToGPULight() const
        {
            GPULight gpu{};
            gpu.PositionAndType = glm::vec4(Position, static_cast<float>(static_cast<uint32_t>(Type)));
            gpu.DirectionAndRange = glm::vec4(Direction, Range);
            gpu.ColorAndIntensity = glm::vec4(Color, Intensity);
            gpu.SpotAngles        = glm::vec4(glm::cos(InnerConeAngle), glm::cos(OuterConeAngle), 0.0f, 0.0f);
            return gpu;
        }
    };
}
