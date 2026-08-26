#pragma once
#include <cstddef>
#include <type_traits>

namespace crimson
{
    struct ShaderPropertyInfo
    {
        std::size_t Offset;
        std::size_t Size;
    };

    template<typename T>
    concept MaterialProperty = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;
}
