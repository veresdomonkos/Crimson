#include "crimson/asset_manager/meta_file.hpp"
#include <fstream>
#include "crimson/core/log.hpp"

namespace crimson::meta
{
    std::filesystem::path MetaPathFor(const std::filesystem::path& src)
    {
        auto p = src;
        p += ".meta";
        return p;
    }

    std::optional<AssetMetadata> Read(const std::filesystem::path& metaPath)
    {
        std::ifstream file(metaPath);
        if (!file) return std::nullopt;

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);

            AssetMetadata m;
            m.Id = AssetID::FromString(j.at("guid").get<std::string>());
            m.Type = AssetTypeFromString(j.at("type").get<std::string>());
            m.ImporterVersion = j.value("importerVersion", 1u);
            m.Settings = j.value("settings", nlohmann::json::object());
            return m;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[Assets] Corrupt meta file {}: {}", metaPath.string(), e.what());
            return std::nullopt;
        }
    }

    bool Write(const std::filesystem::path& metaPath, const AssetMetadata& m)
    {
        nlohmann::json j;
        j["guid"] = m.Id.ToString();
        j["type"] = std::string(ToString(m.Type));
        j["importerVersion"]= m.ImporterVersion;
        j["settings"] = m.Settings;

        std::ofstream file(metaPath);
        if (!file) return false;
        file << j.dump(2);
        return true;
    }
}