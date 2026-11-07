#pragma once
#include <cstdint>

namespace crimson
{
    // Which version of a material's shader a draw uses
    enum class ShaderPass : uint8_t
    {
        Main,
        Depth // for passes that only need depth, like shadows: VSMain, and PSDepth if the shader has one, drawn as opaque
    };
}
