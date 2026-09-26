#pragma once
#include <cstdint>
#include <type_traits>

namespace crimson
{
    struct ShaderPropertyInfo
    {
        std::size_t Offset;
        std::size_t Size;
    };

    struct ShaderTextureBinding
    {
        uint32_t Binding = 0;
    };

    template<typename T>
    concept MaterialProperty = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;
}
