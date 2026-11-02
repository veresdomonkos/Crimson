#include "asset_ref.hpp"

#include <exception>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    // The asset a reference's guid names, if it still exists
    template <typename Json>
    static AssetID ResolveGuid(const AssetManager& assets, const Json& ref)
    {
        if (ref.contains("guid") && ref["guid"].is_string())
        {
            try
            {
                const AssetID id = AssetID::FromString(ref["guid"].template get<std::string>());
                if (id && assets.GetInfo(id))
                    return id;
            }
            catch (const std::exception&) {}
        }

        return {};
    }

    AssetID ResolveAssetRef(const AssetManager& assets, const nlohmann::json& ref, std::string_view context)
    {
        if (ref.is_string())
            return assets.FindId(ref.get<std::string>());

        if (!ref.is_object())
            return {};

        if (const AssetID id = ResolveGuid(assets, ref))
            return id;

        if (ref.contains("path") && ref["path"].is_string())
        {
            const std::string path = ref["path"].get<std::string>();
            if (const AssetID byPath = assets.FindId(path))
            {
                LOG_WARN("[Assets] {}: stale guid, resolved by path '{}'", context, path);
                return byPath;
            }
        }

        return {};
    }

    nlohmann::ordered_json WriteAssetRef(const AssetManager& assets, AssetID id)
    {
        const AssetInfo* info = assets.GetInfo(id);
        return info ? nlohmann::ordered_json{ { "guid", id.ToString() }, { "path", info->Path } } : nlohmann::ordered_json{};
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
