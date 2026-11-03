#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_id.hpp"

namespace crimson
{
    enum class AssetType : uint8_t { None, Texture, Mesh, Shader, Material, Scene };

    inline std::string_view ToString(AssetType t)
    {
        switch (t)
        {
            case AssetType::Texture:  return "Texture";
            case AssetType::Mesh:     return "Mesh";
            case AssetType::Shader:   return "Shader";
            case AssetType::Material: return "Material";
            case AssetType::Scene:    return "Scene";
            default:                  return "None";
        }
    }

    inline AssetType AssetTypeFromString(std::string_view s)
    {
        if (s == "Texture")  return AssetType::Texture;
        if (s == "Mesh")     return AssetType::Mesh;
        if (s == "Shader")   return AssetType::Shader;
        if (s == "Material") return AssetType::Material;
        if (s == "Scene")    return AssetType::Scene;
        return AssetType::None;
    }

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

    struct AssetInfo
    {
        AssetID Id;
        AssetType Type = AssetType::None;
        std::string Path; // relative to the asset folder, with forward slashes
        bool ReadOnly = false; // one of the engine's own, built into the executable
        uint32_t ImporterVersion = 1;
        nlohmann::json Settings = nlohmann::json::object();
    };

    // One of the engine's assets that only the renderer can use, like the tone mapping shader, which pickers leave out
    [[nodiscard]] inline bool IsInternal(const AssetInfo& info)
    {
        return info.ReadOnly && (info.Path.starts_with("engine/shaders/internal/") || info.Path.starts_with("engine/materials/internal/"));
    }
}

template <crimson::AssetType T>
struct std::hash<crimson::TypedAssetID<T>>
{
    size_t operator()(const crimson::TypedAssetID<T>& id) const noexcept
    {
        return std::hash<crimson::AssetID>{}(id);
    }
};
