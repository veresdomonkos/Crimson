#pragma once
#include <algorithm>
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

    // What a pass clears its target to before drawing, the parts not in Flags keep what was there
    struct ClearInfo
    {
        ClearFlags Flags = ClearFlags::None;
        glm::vec4 Color{0, 0, 0, 1};
        float Depth = 1.0f;
        uint32_t Stencil = 0;
    };

    // A camera to draw with, what a shader gets from GetView()
    struct ViewData
    {
        glm::mat4 ViewProj{1.0f};
        glm::vec3 CameraPosition{0.0f};
    };

    // One of a frame's views, and the part of the target its draws land in
    struct FrameView
    {
        ViewData View;

        // The area of the draw's pass when not set
        std::optional<RenderRect> Viewport;
    };

    // Where both rects are, empty (no width or height) when they don't overlap
    [[nodiscard]] inline RenderRect Intersect(const RenderRect& a, const RenderRect& b)
    {
        const int64_t left = std::max(a.X, b.X);
        const int64_t top = std::max(a.Y, b.Y);
        const int64_t right = std::min(int64_t{a.X} + a.Width, int64_t{b.X} + b.Width);
        const int64_t bottom = std::min(int64_t{a.Y} + a.Height, int64_t{b.Y} + b.Height);

        if (right <= left || bottom <= top)
            return RenderRect{ static_cast<int32_t>(left), static_cast<int32_t>(top), 0, 0 };

        return RenderRect{ static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) };
    }

    // The part of the pass's area a view's draws land in. A view can't draw outside its pass.
    [[nodiscard]] inline RenderRect ViewArea(const FrameView& view, const RenderRect& passArea)
    {
        return view.Viewport ? Intersect(*view.Viewport, passArea) : passArea;
    }

    [[nodiscard]] inline bool IsEmpty(const RenderRect& rect) { return rect.Width == 0 || rect.Height == 0; }
}
