#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <cstdio>

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

    // A type of asset, known by the name its files have. Only AssetType::Of makes one, from a type's struct, so the name
    // is always the type's own and lives as long as the program. A default one is no type.
    class AssetType
    {
    public:
        constexpr AssetType() = default;

        template <typename T>
        [[nodiscard]] static constexpr AssetType Of() { return AssetType(T::Name); }

        template <typename T>
        [[nodiscard]] constexpr bool Is() const { return *this == Of<T>(); }

        // Empty for none
        [[nodiscard]] constexpr std::string_view Name() const { return m_name; }

        constexpr explicit operator bool() const { return !m_name.empty(); }
        bool operator==(const AssetType&) const = default; // by the name's contents

    private:
        constexpr explicit AssetType(std::string_view name) : m_name(name) {}

        std::string_view m_name;
    };

    // What an asset's file says, for the types whose files hold nothing to edit
    struct NoAssetData {};

    // An AssetID known to be of one type, so code wanting a shader can't be given a texture by mistake.
    // Only AssetManager makes one from an AssetID, after checking the type. Forgetting the type is always fine.
    // Being of the right type doesn't make it loadable: its file can still be broken.
    template <typename T>
    class TypedAssetID
    {
    public:
        TypedAssetID() = default;

        operator AssetID() const { return m_id; }
        explicit operator bool() const { return static_cast<bool>(m_id); }
        bool operator==(const TypedAssetID&) const = default;

        // Both would turn into AssetIDs and compare, though a texture is never a mesh
        template <typename U> requires (!std::is_same_v<U, T>)
        bool operator==(const TypedAssetID<U>&) const = delete;

        [[nodiscard]] std::string ToString() const { return m_id.ToString(); }
    private:
        friend class AssetManager;
        explicit TypedAssetID(AssetID id) : m_id(id) {}

        AssetID m_id;
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

template <typename T>
struct std::hash<crimson::TypedAssetID<T>>
{
    size_t operator()(const crimson::TypedAssetID<T>& id) const noexcept
    {
        return std::hash<crimson::AssetID>{}(id);
    }
};

template<>
struct std::hash<crimson::AssetType>
{
    size_t operator()(const crimson::AssetType& type) const noexcept
    {
        return std::hash<std::string_view>{}(type.Name());
    }
};
