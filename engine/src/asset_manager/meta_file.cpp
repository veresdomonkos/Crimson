#include "crimson/asset_manager/meta_file.hpp"
#include <fstream>
#include <exception>
#include "crimson/core/log.hpp"
#include <crimson/core/file_write.hpp>

namespace crimson::meta
{
    std::filesystem::path PathFor(const std::filesystem::path& src)
    {
        auto p = src;
        p += ".meta";
        return p;
    }

    std::optional<AssetInfo> Read(const std::filesystem::path& metaPath, AssetType importerType)
    {
        std::ifstream file(metaPath);
        if (!file) return std::nullopt;

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);

            AssetInfo m;
            m.Id = AssetID::FromString(j.at("guid").get<std::string>());
            // The importer decides what the file is, so a meta that says otherwise is only worth a warning
            const std::string written = j.at("type").get<std::string>();
            if (written != importerType.Name())
                LOG_WARN("[Assets] {} says it is a {}, but its importer makes {}, which it is taken for", metaPath.string(), written, importerType.Name());

            m.Type = importerType;
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

    bool Write(const std::filesystem::path& metaPath, const AssetInfo& m)
    {
        nlohmann::json j;
        j["guid"] = m.Id.ToString();
        j["type"] = std::string(m.Type.Name());
        j["importerVersion"]= m.ImporterVersion;
        j["settings"] = m.Settings;

        return WriteFileSafely(metaPath, j.dump(2));
    }
}
