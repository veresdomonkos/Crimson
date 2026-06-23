#pragma once
#include "resource_handles.hpp"
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

    constexpr bool HasClearFlag(const ClearFlags value, const ClearFlags flag)
    {
        return (value & flag) != ClearFlags::None;
    }

    struct RenderPassInfo
    {
        RenderTargetHandle Target;
        ClearFlags ClearFlags;
        glm::vec4 ClearColor;
        float ClearDepth = 1.0f;
        uint32_t ClearStencil = 0;
    };
}
