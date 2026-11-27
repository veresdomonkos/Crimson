#include "asset_ref.hpp"

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
    static AssetID ReadGuid(const AssetManager& assets, const Json& ref)
    {
        const AssetID id = assets.Redirect(ReadGuid(ref));
        return id && assets.GetInfo(id) ? id : AssetID{};
    }

    static AssetID RememberMissing(const AssetManager& assets, AssetID id, std::string path, AssetType type, std::string_view context)
    {
        const bool pathOnly = !id;
        if (pathOnly)
            id = AssetID::FromName(kAssetPathNamespace, path);

        const std::string shown = path.empty() ? id.ToString() : path;

        if (assets.RememberMissing(id, { .Path = std::move(path), .PathOnly = pathOnly, .Type = type }))
            LOG_WARN("[Assets] {}: {} is missing, the reference is kept for when it is back", context, shown);

        return id;
    }

    // A missing one is remembered as the type it was expected to be
    static AssetID ReadAnyAssetRef(const AssetManager& assets, const nlohmann::json& ref, AssetType expected, std::string_view context)
    {
        AssetRef parsed = ParseAssetRef(ref);

        if (const AssetID id = assets.Redirect(parsed.Id); id && assets.GetInfo(id))
            return id;

        if (const AssetID byPath = parsed.Path.empty() ? AssetID{} : assets.FindId(parsed.Path))
        {
            if (parsed.Id)
                LOG_WARN("[Assets] {}: stale guid, found by path '{}'", context, parsed.Path);

            return byPath;
        }

        return parsed ? RememberMissing(assets, parsed.Id, std::move(parsed.Path), expected, context) : AssetID{};
    }

    AssetRef ParseAssetRef(const nlohmann::json& ref)
    {
        if (ref.is_string())
            return { {}, ref.get<std::string>() };

        if (!ref.is_object())
            return {};

        return { ReadGuid(ref), ref.contains("path") && ref["path"].is_string() ? ref["path"].get<std::string>() : std::string() };
    }

    nlohmann::ordered_json WriteAssetRef(const AssetRef& ref)
    {
        if (ref.Id)
        {
            nlohmann::ordered_json json = { { "guid", ref.Id.ToString() } };
            if (!ref.Path.empty())
                json["path"] = ref.Path;

            return json;
        }

        if (!ref.Path.empty())
            return ref.Path;

        return {};
    }

    AssetID ReadAssetRef(const AssetManager& assets, const nlohmann::json& ref, AssetType expected, std::string_view context)
    {
        if (!expected)
            return {};

        const AssetID id = ReadAnyAssetRef(assets, ref, expected, context);
        if (!id)
            return id;

        // What AssetManager::As checks
        const AssetInfo* info = assets.GetInfo(id);
        const MissingAssetRef* missing = info ? nullptr : assets.GetMissing(id);
        const AssetType actual = info ? info->Type : missing ? missing->Type : AssetType{};

        if (actual == expected)
            return id;

        // A broken file rather than one that is away for a while, so it isn't kept
        LOG_WARN("[Assets] {}: {} isn't a {}", context, info ? info->Path : id.ToString(), expected.Name());
        return {};
    }

    nlohmann::ordered_json WriteAssetRef(const AssetManager& assets, AssetID id)
    {
        if (const AssetInfo* info = assets.GetInfo(id))
            return WriteAssetRef(AssetRef{ id, info->Path });

        // Written back as it was read, for it to work again once the file is back
        if (const MissingAssetRef* missing = assets.GetMissing(id))
            return WriteAssetRef(AssetRef{ missing->PathOnly ? AssetID{} : id, missing->Path });

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
            if (const AssetID id = ReadGuid(assets, ref); id && id != it->second)
                return false;
        }

        ref = WriteAssetRef(assets, it->second);
        return true;
    }
}
