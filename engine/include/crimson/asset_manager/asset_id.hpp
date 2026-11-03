#pragma once
#include <cstdint>
#include <functional>
#include <random>
#include <string>
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

    enum class AssetType : uint8_t { None, Texture, Mesh, Shader, Material, Scene };

    // An AssetID known to be of one type, so code wanting a shader can't be given a texture by mistake.
    // Only AssetManager makes one from an AssetID, after checking the type. Forgetting the type is always fine.
    // Being of the right type doesn't make it loadable: its file can still be broken.
    template <AssetType T>
    class TypedAssetID
    {
    public:
        static constexpr AssetType Type = T;

        TypedAssetID() = default;

        operator AssetID() const { return m_id; }
        explicit operator bool() const { return static_cast<bool>(m_id); }
        bool operator==(const TypedAssetID&) const = default;

        [[nodiscard]] std::string ToString() const { return m_id.ToString(); }
    private:
        friend class AssetManager;
        explicit TypedAssetID(AssetID id) : m_id(id) {}

        AssetID m_id;
    };

    using TextureID  = TypedAssetID<AssetType::Texture>;
    using MeshID     = TypedAssetID<AssetType::Mesh>;
    using ShaderID   = TypedAssetID<AssetType::Shader>;
    using MaterialID = TypedAssetID<AssetType::Material>;
    using SceneID    = TypedAssetID<AssetType::Scene>;
}

template<>
struct std::hash<crimson::AssetID>
{
    size_t operator()(const crimson::AssetID& id) const noexcept
    {
        return std::hash<uint64_t>{}(id.Value);
    }
};

template <crimson::AssetType T>
struct std::hash<crimson::TypedAssetID<T>>
{
    size_t operator()(const crimson::TypedAssetID<T>& id) const noexcept
    {
        return std::hash<crimson::AssetID>{}(id);
    }
};
