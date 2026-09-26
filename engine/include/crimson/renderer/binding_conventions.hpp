#pragma once

#include <cstdint>

namespace crimson
{
    constexpr uint32_t kCameraBlockBinding   = 0; // set=0, UBO (dynamic, per-pass)
    constexpr uint32_t kLightingBlockBinding = 1; // set=0, UBO (dynamic, per-frame)
    constexpr uint32_t kShadowMapBinding     = 2; // set=0, sampler2D

    constexpr uint32_t kMaterialBindingStart = 3; // set=1, every material UBO/sampler >= than this

    constexpr uint32_t kMaxLights = 16;
}