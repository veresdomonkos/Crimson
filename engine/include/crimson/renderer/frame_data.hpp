#pragma once
#include <chrono>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

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
        float Time = 0.0f;      // seconds since the first frame
        float DeltaTime = 0.0f; // seconds since the frame before
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

    struct FrameTime
    {
        float Time = 0.0f;
        float DeltaTime = 0.0f;

        bool operator==(const FrameTime&) const = default;
    };

    // Measures the time shaders see. The first frame is at 0, with no time since a frame before it.
    class FrameClock
    {
    public:
        FrameTime Tick(std::chrono::steady_clock::time_point now)
        {
            if (!m_start)
            {
                m_start = now;
                m_previous = now;
            }

            const FrameTime time{ Seconds(now - *m_start), Seconds(now - m_previous) };
            m_previous = now;
            return time;
        }
    private:
        static float Seconds(std::chrono::steady_clock::duration duration) { return std::chrono::duration<float>(duration).count(); }

        std::optional<std::chrono::steady_clock::time_point> m_start;
        std::chrono::steady_clock::time_point m_previous;
    };

    // The frame's lighting and time in the form they are uploaded, the vectors keep their capacity between frames
    struct FrameGpuData
    {
        FrameBlock Block;
        std::vector<GpuLight> Lights;
        std::vector<GpuShadow> Shadows;

        void Build(const FrameLightingData& lighting, FrameTime time)
        {
            Block.Time = time.Time;
            Block.DeltaTime = time.DeltaTime;

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
