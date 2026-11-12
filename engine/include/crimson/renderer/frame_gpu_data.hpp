#pragma once
#include <chrono>
#include <optional>
#include <span>
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

    struct GpuView
    {
        glm::mat4 ViewProj;
        glm::vec4 CameraPosition;
    };
    static_assert(sizeof(GpuView) == 80);

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

    struct GpuObject
    {
        glm::mat4 Transform;
        glm::mat4 NormalMatrix;
    };
    static_assert(sizeof(GpuObject) == 128);

    // One tile of the shadow atlas, with what a light sees from it
    struct ShadowView
    {
        GpuShadow Shadow;
        RenderRect Viewport;

        // Where the light looks from, the camera of the shadow pass's draws for this view
        glm::vec3 Eye{0.0f};
    };

    struct FrameLighting
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

    // The frame's lighting, views and time in the form they are uploaded, the vectors keep their capacity between frames
    struct FrameGpuData
    {
        FrameBlock Block;
        std::vector<GpuLight> Lights;
        std::vector<GpuShadow> Shadows;
        std::vector<GpuView> Views;

        void Build(const FrameLighting& lighting, std::span<const FrameView> views, FrameTime time)
        {
            Block.Time = time.Time;
            Block.DeltaTime = time.DeltaTime;

            Views.clear();
            for (const FrameView& view : views)
                Views.push_back({ view.View.ViewProj, glm::vec4(view.View.CameraPosition, 0.0f) });

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
