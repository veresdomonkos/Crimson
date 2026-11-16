#pragma once
#include <cstdint>
#include <string_view>

namespace crimson
{
    inline constexpr uint64_t kFnv1aOffset = 14695981039346656037ull;

    // FNV-1a. Pass the last result as hash to continue it over more bytes.
    [[nodiscard]] constexpr uint64_t Fnv1a(std::string_view bytes, uint64_t hash = kFnv1aOffset)
    {
        for (const char c : bytes)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }

        return hash;
    }
}
