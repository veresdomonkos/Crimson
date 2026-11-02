#pragma once
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace crimson
{
    struct ShaderPropertyInfo
    {
        std::size_t Offset;
        std::size_t Size;

        // From the shader's annotations. A property without a default starts at zero.
        std::vector<float> Default;
        std::optional<std::pair<float, float>> Range;
    };

    template<typename T>
    concept MaterialProperty = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;
}
