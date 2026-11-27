#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace crimson
{
    // SHA-1 as FIPS 180-4 defines it. It is here for name-based UUIDs (RFC 9562 version 5), not for security.
    [[nodiscard]] std::array<std::uint8_t, 20> Sha1(std::string_view bytes);
}
