#include "sha1.hpp"

#include <bit>
#include <cstddef>
#include <cstring>

namespace crimson
{
    static void Compress(std::uint32_t (&state)[5], const std::uint8_t* block)
    {
        std::uint32_t w[80];

        // Big-endian by hand, so the host's byte order doesn't matter
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (std::uint32_t{ block[i * 4] } << 24) | (std::uint32_t{ block[i * 4 + 1] } << 16)
                 | (std::uint32_t{ block[i * 4 + 2] } << 8) | std::uint32_t{ block[i * 4 + 3] };
        }

        for (int i = 16; i < 80; ++i)
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f, k;

            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else             { f = b ^ c ^ d;                   k = 0xca62c1d6; }

            const std::uint32_t temp = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = temp;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    std::array<std::uint8_t, 20> Sha1(std::string_view bytes)
    {
        std::uint32_t state[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        const std::size_t size = bytes.size();

        std::size_t offset = 0;
        for (; size - offset >= 64; offset += 64)
            Compress(state, data + offset);

        // The tail plus 0x80 and the 8-byte length takes one block, or two when the tail is 56 bytes or more
        std::uint8_t tail[128] = {};
        const std::size_t rest = size - offset;
        if (rest > 0)
            std::memcpy(tail, data + offset, rest);

        tail[rest] = 0x80;
        const std::size_t tailSize = rest < 56 ? 64 : 128;

        const std::uint64_t bits = std::uint64_t{ size } * 8;
        for (int i = 0; i < 8; ++i)
            tail[tailSize - 1 - i] = static_cast<std::uint8_t>(bits >> (i * 8));

        for (std::size_t i = 0; i < tailSize; i += 64)
            Compress(state, tail + i);

        std::array<std::uint8_t, 20> digest{};
        for (int i = 0; i < 20; ++i)
            digest[i] = static_cast<std::uint8_t>(state[i / 4] >> (24 - (i % 4) * 8));

        return digest;
    }
}
