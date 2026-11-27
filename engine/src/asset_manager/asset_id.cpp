#include "crimson/asset_manager/asset_id.hpp"

#include <cstring>
#include <random>
#include <stdexcept>

#include "core/sha1.hpp"

namespace crimson
{
    constexpr std::size_t kTextLength = 36;
    constexpr std::array<std::size_t, 4> kHyphens = { 8, 13, 18, 23 };

    static bool IsHyphen(std::size_t index)
    {
        for (const std::size_t h : kHyphens)
            if (h == index)
                return true;

        return false;
    }

    // The version in the high nibble of byte 6, and the RFC variant (10) in the top bits of byte 8
    static void SetVersion(AssetID& id, std::uint8_t version)
    {
        id.Bytes[6] = static_cast<std::uint8_t>((id.Bytes[6] & 0x0f) | (version << 4));
        id.Bytes[8] = static_cast<std::uint8_t>((id.Bytes[8] & 0x3f) | 0x80);
    }

    static int HexValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    AssetID AssetID::Generate()
    {
        static_assert(sizeof(unsigned int) == 4);

        // One random_device isn't safe to call from two threads at once
        thread_local std::random_device device;

        AssetID id;
        for (std::size_t i = 0; i < id.Bytes.size(); i += 4)
        {
            const unsigned int word = device();
            std::memcpy(id.Bytes.data() + i, &word, 4);
        }

        SetVersion(id, 4);
        return id;
    }

    AssetID AssetID::FromName(const AssetID& nameSpace, std::string_view name)
    {
        std::string input(reinterpret_cast<const char*>(nameSpace.Bytes.data()), nameSpace.Bytes.size());
        input += name;

        const std::array<std::uint8_t, 20> digest = Sha1(input);

        AssetID id;
        std::memcpy(id.Bytes.data(), digest.data(), id.Bytes.size());
        SetVersion(id, 5);
        return id;
    }

    std::string AssetID::ToString() const
    {
        constexpr char digits[] = "0123456789abcdef";

        std::string text;
        text.reserve(kTextLength);

        std::size_t byte = 0;
        for (std::size_t i = 0; i < kTextLength; ++i)
        {
            if (IsHyphen(i))
            {
                text += '-';
                continue;
            }

            const std::uint8_t b = Bytes[byte / 2];
            text += digits[byte % 2 == 0 ? b >> 4 : b & 0x0f];
            ++byte;
        }

        return text;
    }

    AssetID AssetID::FromString(std::string_view text)
    {
        if (text.size() != kTextLength)
            throw std::invalid_argument("An id is 36 characters, like 8-4-4-4-12 hex digits");

        AssetID id;
        std::size_t nibble = 0;
        for (std::size_t i = 0; i < kTextLength; ++i)
        {
            if (IsHyphen(i))
            {
                if (text[i] != '-')
                    throw std::invalid_argument("An id has hyphens after the 8th, 12th, 16th and 20th digits");

                continue;
            }

            const int value = HexValue(text[i]);
            if (value < 0)
                throw std::invalid_argument("An id has only hex digits between its hyphens");

            id.Bytes[nibble / 2] = static_cast<std::uint8_t>(id.Bytes[nibble / 2] | (nibble % 2 == 0 ? value << 4 : value));
            ++nibble;
        }

        return id;
    }
}
