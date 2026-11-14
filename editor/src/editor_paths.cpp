#include "crimson_editor/editor_paths.hpp"

#include <system_error>

#include <crimson/core/log.hpp>

#include "crimson_editor/platform.hpp"

namespace fs = std::filesystem;

namespace crimson::editor
{
    fs::path GetUserConfigFolder()
    {
        const fs::path folder = platform::UserConfigFolder();

        std::error_code error;
        fs::create_directories(folder, error);
        if (error)
            LOG_WARN("[Editor] Can't create {}: {}", folder.string(), error.message());

        return folder;
    }

    fs::path GetDefaultProjectsFolder()
    {
        const fs::path home = platform::HomeFolder();

        std::error_code error;
        const fs::path documents = home / "Documents";
        return fs::is_directory(documents, error) ? documents : home;
    }
}
