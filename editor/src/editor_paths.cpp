#include "crimson_editor/editor_paths.hpp"

#include <system_error>

#include <crimson/core/log.hpp>

#include "crimson_editor/platform.hpp"

namespace fs = std::filesystem;

namespace crimson::editor::paths
{
    fs::path UserConfigFolder()
    {
        const fs::path folder = platform::UserConfigFolder();

        std::error_code error;
        fs::create_directories(folder, error);
        if (error)
            LOG_WARN("[Editor] Can't create {}: {}", folder.string(), error.message());

        return folder;
    }

    fs::path EngineCacheFolder()
    {
        const fs::path overridden = platform::EnvironmentPath("CRIMSON_CACHE");
        return overridden.empty() ? platform::UserCacheFolder() : overridden;
    }

    fs::path DefaultProjectsFolder()
    {
        const fs::path home = platform::HomeFolder();

        std::error_code error;
        const fs::path documents = home / "Documents";
        return fs::is_directory(documents, error) ? documents : home;
    }
}
