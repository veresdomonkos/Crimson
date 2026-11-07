#include "asset_ref.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    // The guid a reference was written with, whether its asset exists or not
    template <typename Json>
    static AssetID ReadGuid(const Json& ref)
    {
        if (ref.contains("guid") && ref["guid"].is_string())
        {
            try
            {
                return AssetID::FromString(ref["guid"].template get<std::string>());
            }
            catch (const std::exception&) {}
        }

        return {};
    }

    // The asset a reference's guid names, if it still exists
    template <typename Json>
    static AssetID ResolveGuid(const AssetManager& assets, const Json& ref)
    {
        const AssetID id = assets.Redirect(ReadGuid(ref));
        return id && assets.GetInfo(id) ? id : AssetID{};
    }

    // An id for a reference written as only a path, the same every time it is read. FNV-1a.
    static AssetID IdForPath(std::string_view path)
    {
        uint64_t hash = 14695981039346656037ull;

        for (const char c : path)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }

        return AssetID{ hash != 0 ? hash : 1 };
    }

    static AssetID RememberMissing(const AssetManager& assets, AssetID id, std::string path, AssetType type, std::string_view context)
    {
        const bool pathOnly = !id;
        if (pathOnly)
            id = IdForPath(path);

        const std::string shown = path.empty() ? id.ToString() : path;

        if (assets.RememberMissing(id, { .Path = std::move(path), .PathOnly = pathOnly, .Type = type }))
            LOG_WARN("[Assets] {}: {} is missing, the reference is kept for when it is back", context, shown);

        return id;
    }

    // A missing one is remembered as the type it was expected to be
    static AssetID ResolveAnyAssetRef(const AssetManager& assets, const nlohmann::json& ref, AssetType expected, std::string_view context)
    {
        if (ref.is_string())
        {
            std::string path = ref.get<std::string>();
            if (const AssetID id = assets.FindId(path))
                return id;

            return path.empty() ? AssetID{} : RememberMissing(assets, {}, std::move(path), expected, context);
        }

        if (!ref.is_object())
            return {};

        if (const AssetID id = ResolveGuid(assets, ref))
            return id;

        std::string path = ref.contains("path") && ref["path"].is_string() ? ref["path"].get<std::string>() : std::string();

        if (const AssetID byPath = path.empty() ? AssetID{} : assets.FindId(path))
        {
            LOG_WARN("[Assets] {}: stale guid, resolved by path '{}'", context, path);
            return byPath;
        }

        const AssetID guid = ReadGuid(ref);
        return guid || !path.empty() ? RememberMissing(assets, guid, std::move(path), expected, context) : AssetID{};
    }

    template <AssetType T>
    TypedAssetID<T> ResolveAssetRef(const AssetManager& assets, const nlohmann::json& ref, std::string_view context)
    {
        const AssetID id = ResolveAnyAssetRef(assets, ref, T, context);
        const TypedAssetID<T> typed = assets.As<T>(id);

        // A broken file rather than one that is away for a while, so it isn't kept
        if (id && !typed)
        {
            const AssetInfo* info = assets.GetInfo(id);
            LOG_WARN("[Assets] {}: {} isn't a {}", context, info ? info->Path : id.ToString(), ToString(T));
        }

        return typed;
    }

    template TextureID ResolveAssetRef(const AssetManager&, const nlohmann::json&, std::string_view);
    template MeshID ResolveAssetRef(const AssetManager&, const nlohmann::json&, std::string_view);
    template ShaderID ResolveAssetRef(const AssetManager&, const nlohmann::json&, std::string_view);
    template MaterialID ResolveAssetRef(const AssetManager&, const nlohmann::json&, std::string_view);
    template SceneID ResolveAssetRef(const AssetManager&, const nlohmann::json&, std::string_view);

    nlohmann::ordered_json WriteAssetRef(const AssetManager& assets, AssetID id)
    {
        if (const AssetInfo* info = assets.GetInfo(id))
            return { { "guid", id.ToString() }, { "path", info->Path } };

        // Written back as it was read, for it to work again once the file is back
        if (const MissingAssetRef* missing = assets.GetMissing(id))
        {
            if (missing->PathOnly)
                return missing->Path;

            nlohmann::ordered_json ref = { { "guid", id.ToString() } };
            if (!missing->Path.empty())
                ref["path"] = missing->Path;

            return ref;
        }

        return {};
    }

    bool UpdateAssetRef(nlohmann::ordered_json& ref, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets)
    {
        const bool hasPath = ref.is_object() && ref.contains("path") && ref["path"].is_string();
        if (!ref.is_string() && !hasPath)
            return false;

        const auto it = moved.find(hasPath ? ref["path"].get<std::string>() : ref.get<std::string>());
        if (it == moved.end())
            return false;

        // The id wins over the path when reading, so this one was about something else that used to be there
        if (hasPath)
        {
            if (const AssetID id = ResolveGuid(assets, ref); id && id != it->second)
                return false;
        }

        ref = WriteAssetRef(assets, it->second);
        return true;
    }
}
