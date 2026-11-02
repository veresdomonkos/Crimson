#pragma once

namespace crimson
{
    // How the linear HDR scene is fitted into what a display can show
    enum class ToneMapper
    {
        Aces,
        None // only clamps
    };
}
