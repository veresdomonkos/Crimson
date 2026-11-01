#pragma once
#include <cstdint>
#include <optional>

#include "camera_data.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "glm/vec4.hpp"

namespace crimson
{
    enum class ClearFlags : uint32_t
    {
        None   = 0,
        Color  = 1 << 0,
        Depth  = 1 << 1,
        Stencil = 1 << 2
    };

    inline ClearFlags operator|(ClearFlags a, ClearFlags b)
    {
        return static_cast<ClearFlags>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
    }

    inline ClearFlags operator&(ClearFlags a, ClearFlags b)
    {
        return static_cast<ClearFlags>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
    }

    inline ClearFlags& operator|=(ClearFlags& a, ClearFlags b)
    {
        a = a | b;
        return a;
    }

    static bool HasClearFlag(const ClearFlags value, const ClearFlags flag)
    {
        return (value & flag) != ClearFlags::None;
    }

    struct RenderRect
    {
        int32_t X = 0;
        int32_t Y = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    struct RenderPassInfo
    {
        RenderTargetHandle Target;
        ClearFlags ClearFlags = ClearFlags::None;
        glm::vec4 ClearColor{0, 0, 0, 1};
        float ClearDepth = 1.0f;
        uint32_t ClearStencil = 0;

        glm::mat4 ViewProj{1.0f};
        glm::vec3 CameraPosition{0.0f};

        // Limits drawing and clearing to part of the target, the whole target when not set
        std::optional<RenderRect> Viewport;
    };
}
