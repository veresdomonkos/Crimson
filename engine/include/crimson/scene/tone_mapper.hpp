#pragma once

namespace crimson
{
    // How the linear HDR scene is fitted into what a display can show
    enum class ToneMapper
    {
        Gt7,
        None // only clamps
    };
}
