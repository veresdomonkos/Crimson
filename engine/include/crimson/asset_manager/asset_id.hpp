#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include "crimson/core/hash.hpp"

namespace crimson
{
    // A 128-bit GUID, an RFC 9562 UUID. Bytes are in the order of the text, so byte 0 is its first two digits.
    struct AssetID
    {
        std::array<std::uint8_t, 16> Bytes{};

        explicit operator bool() const
        {
            for (const std::uint8_t b : Bytes)
                if (b != 0)
                    return true;

            return false;
        }

        bool operator==(const AssetID&) const = default;

        // A random one (version 4), from the system's entropy
        static AssetID Generate();

        // The version 5 UUID of a name in a namespace, the same every time, for ids that can't be stored anywhere
        static AssetID FromName(const AssetID& nameSpace, std::string_view name);

        // Lowercase xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
        [[nodiscard]] std::string ToString() const;

        // Throws std::invalid_argument unless the text is exactly that, in either case
        static AssetID FromString(std::string_view text);
    };

    // The namespace of ids named by an asset path, such as engine/meshes/SM_Cube.obj. It is part of the file format and
    // never changes, as that would change the id of every engine resource. Engine resources and references written as
    // only a path share it on purpose: the same path is the same asset.
    inline constexpr AssetID kAssetPathNamespace{ { 0x15, 0xe9, 0x77, 0x74, 0x22, 0xc0, 0x47, 0xd0, 0x83, 0x83, 0x61, 0xa4, 0x5b, 0xeb, 0x95, 0xb2 } };

    // An asset reference as a file holds it, not yet looked up, for files read before there are assets (such as the
    // project file). The path is relative to assets/ and finds the asset if the id is stale. A path with no id is a
    // hand-written one. AssetManager::FindId looks it up.
    struct AssetRef
    {
        AssetID Id;
        std::string Path;

        explicit operator bool() const { return Id || !Path.empty(); }
        bool operator==(const AssetRef&) const = default;
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
        // Only for hash tables, not for identity
        return static_cast<size_t>(crimson::Fnv1a(std::string_view(reinterpret_cast<const char*>(id.Bytes.data()), id.Bytes.size())));
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
