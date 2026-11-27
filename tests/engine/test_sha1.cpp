#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>

#include "core/sha1.hpp"

using namespace crimson;

static std::string Hex(const std::array<std::uint8_t, 20>& digest)
{
    constexpr char digits[] = "0123456789abcdef";

    std::string text;
    for (const std::uint8_t b : digest)
    {
        text += digits[b >> 4];
        text += digits[b & 0x0f];
    }

    return text;
}

TEST_CASE("SHA-1 matches the FIPS 180 test vectors", "[sha1]")
{
    CHECK(Hex(Sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(Hex(Sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    // 56 bytes, so the padding needs a second block
    CHECK(Hex(Sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    CHECK(Hex(Sha1(std::string(1000000, 'a'))) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    CHECK(Hex(Sha1("The quick brown fox jumps over the lazy dog")) == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

TEST_CASE("SHA-1 pads every tail length", "[sha1]")
{
    CHECK(Hex(Sha1(std::string(55, 'a'))) == "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    CHECK(Hex(Sha1(std::string(56, 'a'))) == "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    CHECK(Hex(Sha1(std::string(63, 'a'))) == "03f09f5b158a7a8cdad920bddc29b81c18a551f5");
    CHECK(Hex(Sha1(std::string(64, 'a'))) == "0098ba824b5c16427bd7a1122a5a442a25ec644d");
    CHECK(Hex(Sha1(std::string(65, 'a'))) == "11655326c708d70319be2610e8a57d9a5b959d3b");
}
