#pragma once
#include <cstdint>
#include <functional>

namespace crimson
{
    struct NativeContext
    {
        void* Device = nullptr;
        void* CommandBuffer = nullptr;
        uint32_t ColorFormat = 0;
    };

    using NativePassFn = std::function<void(const NativeContext&)>;
}
