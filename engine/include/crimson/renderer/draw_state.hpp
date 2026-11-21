#pragma once

#include "crimson/renderer/frame_recorder.hpp"
#include "crimson/renderer/material_settings.hpp"
#include "crimson/renderer/shader_variant.hpp"

namespace crimson
{
    // The order a draw comes in among the draws of its pass
    enum class DrawSequence
    {
        Sorted,      // grouped by state, so the renderers rebind as little as possible
        BackToFront, // farthest first, so what is blended lies under the nearer
        Recorded     // as the draws were added
    };

    // How a draw is blended and ordered, from what decides it
    struct DrawState
    {
        bool Blend = false;
        bool DepthTest = true;
        bool DepthWrite = true;
        DrawSequence Order = DrawSequence::Sorted;
    };

    // The one place that decides it, for both backends and the sorting. A new pass kind or render mode is added here.
    [[nodiscard]] constexpr DrawState ResolveDrawState(PassKind kind, const MaterialSettings& settings, ShaderVariant variant)
    {
        if (kind == PassKind::Overlay)
            return { .Blend = true, .DepthTest = false, .DepthWrite = false, .Order = DrawSequence::Recorded };

        // A depth version draws everything as opaque, so a transparent mesh still casts its shadow
        const bool transparent = variant == ShaderVariant::Main && settings.RenderMode == MaterialRenderMode::Transparent;

        return { .Blend = transparent, .DepthTest = true, .DepthWrite = !transparent, .Order = transparent ? DrawSequence::BackToFront : DrawSequence::Sorted };
    }
}
