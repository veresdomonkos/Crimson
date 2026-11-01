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

    struct GpuLight
    {
        glm::vec3 Position;
        uint32_t Type;
        glm::vec3 Direction;
        float Range;
        glm::vec3 Color;
        float Intensity;
        float SpotInnerCos;
        float SpotOuterCos;
        int32_t ShadowIndex; // first entry in the shadow buffer, a point light owns six in a row
        uint32_t Pad;
    };
    static_assert(sizeof(GpuLight) == 64);

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
        bool CastsShadow = false;
        int32_t ShadowIndex = -1; // set by BuildShadowViews

        [[nodiscard]] GpuLight ToGpuLight() const
        {
            GpuLight gpu{};
            gpu.Position = Position;
            gpu.Type = static_cast<uint32_t>(Type);
            gpu.Direction = Direction;
            gpu.Range = Range;
            gpu.Color = Color;
            gpu.Intensity = Intensity;
            gpu.SpotInnerCos = glm::cos(InnerConeAngle);
            gpu.SpotOuterCos = glm::cos(OuterConeAngle);
            gpu.ShadowIndex = ShadowIndex;
            return gpu;
        }
    };
}
