#pragma once
#include <cstdint>
#include <functional>
#include <random>
#include <string>

namespace crimson
{
    struct AssetID
    {
        uint64_t Value = 0;

        explicit operator bool() const { return Value != 0; }
        bool operator==(const AssetID&) const = default;

        static AssetID Generate()
        {
            static std::mt19937_64 rng{ std::random_device{}() };
            uint64_t v = 0;
            while (v == 0) v = rng();
            return AssetID{ v };
        }

        [[nodiscard]] std::string ToString() const
        {
            char buf[17];
            std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(Value));
            return buf;
        }

        static AssetID FromString(const std::string& s) { return AssetID{ std::stoull(s, nullptr, 16) }; }
    };
}

template<>
struct std::hash<crimson::AssetID>
{
    size_t operator()(const crimson::AssetID& id) const noexcept
    {
        return std::hash<uint64_t>{}(id.Value);
    }
};