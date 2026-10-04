#pragma once

namespace crimson
{
    class GraphicsDevice
    {
    public:
        virtual void WaitIdle() const = 0;
        virtual ~GraphicsDevice() = default;
    };
}