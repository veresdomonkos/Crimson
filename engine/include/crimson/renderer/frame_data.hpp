#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "light.hpp"
#include "binding_conventions.hpp"

namespace crimson
{
    struct CameraBlock
    {
        glm::mat4 ViewProj;
        glm::vec4 Position;
    };

    struct LightingBlock
    {
        glm::vec4 AmbientColor;
        glm::mat4 ShadowViewProj;
        uint32_t LightCount = 0;
        int32_t  ShadowLightIndex = -1;
        uint32_t _Pad[2]{};
        GPULight Lights[kMaxLights]{};
    };

    struct ObjectBlock
    {
        glm::mat4 Transform;
        glm::mat4 NormalMatrix;
    };

    struct FrameLightingData
    {
        glm::vec3 CameraPosition{0.0f};
        glm::vec3 AmbientColor{0.02f};
        std::vector<Light> Lights;

        int32_t ShadowLightIndex = -1;
        glm::mat4 ShadowViewProj{1.0f};
    };
}