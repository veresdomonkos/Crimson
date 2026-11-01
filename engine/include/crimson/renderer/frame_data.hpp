#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "light.hpp"
#include "core.hpp"

namespace crimson
{
    // These match crimson/bindings.hlsli, shader reflection reports it if they drift apart
    struct FrameBlock
    {
        glm::vec4 AmbientColor{0.0f};
        uint32_t LightCount = 0;
        uint32_t ShadowCount = 0;
        uint32_t Pad[2]{};
    };
    static_assert(sizeof(FrameBlock) == 32);

    struct ViewBlock
    {
        glm::mat4 ViewProj;
        glm::vec4 CameraPosition;
    };
    static_assert(sizeof(ViewBlock) == 80);

    struct GpuShadow
    {
        glm::mat4 ViewProj;
        glm::vec4 AtlasRect; // xy = offset, zw = size, in atlas UVs
        float DepthBias;
        float NormalBias;
        float TexelSize;
        float Pad;
    };
    static_assert(sizeof(GpuShadow) == 96);

    struct ObjectData
    {
        glm::mat4 Transform;
        glm::mat4 NormalMatrix;
    };
    static_assert(sizeof(ObjectData) == 128);

    // One tile of the shadow atlas, rendered by its own pass
    struct ShadowView
    {
        GpuShadow Shadow;
        RenderRect Viewport;
    };

    struct FrameLightingData
    {
        glm::vec3 AmbientColor{0.02f};
        std::vector<Light> Lights;
        std::vector<ShadowView> Shadows;
    };

    // The lighting data in the form it is uploaded, the vectors keep their capacity between frames
    struct FrameGpuData
    {
        FrameBlock Block;
        std::vector<GpuLight> Lights;
        std::vector<GpuShadow> Shadows;

        void Build(const FrameLightingData& lighting)
        {
            Lights.clear();
            for (const Light& light : lighting.Lights)
                Lights.push_back(light.ToGpuLight());

            Shadows.clear();
            for (const ShadowView& view : lighting.Shadows)
                Shadows.push_back(view.Shadow);

            Block.AmbientColor = glm::vec4(lighting.AmbientColor, 0.0f);
            Block.LightCount = static_cast<uint32_t>(Lights.size());
            Block.ShadowCount = static_cast<uint32_t>(Shadows.size());
        }
    };
}
