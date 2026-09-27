#pragma once
#include <cstdint>
#include <functional>

namespace crimson
{
    struct NativeFrameHandles
    {
        void* Device = nullptr;
        void* CommandBuffer = nullptr;
        uint32_t ColorFormat = 0;
    };

    using RawPassCallback = std::function<void(const NativeFrameHandles&)>;
}
