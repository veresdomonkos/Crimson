#include "crimson_editor/editor_paths.hpp"

#include <cstdlib>
#include <system_error>

#include <crimson/core/log.hpp>

namespace fs = std::filesystem;

namespace crimson::editor
{
    // The wide version on Windows, as a user's folder can have non-ASCII characters
    static fs::path GetEnvironmentPath(const char* name, const wchar_t* wideName)
    {
#ifdef _WIN32
        (void)name;
        const wchar_t* value = _wgetenv(wideName);
#else
        (void)wideName;
        const char* value = std::getenv(name);
#endif
        return value ? fs::path(value) : fs::path();
    }

    fs::path GetUserConfigFolder()
    {
#ifdef _WIN32
        fs::path folder = GetEnvironmentPath("APPDATA", L"APPDATA") / "Crimson";
#else
        fs::path base = GetEnvironmentPath("XDG_CONFIG_HOME", L"XDG_CONFIG_HOME");
        if (base.empty())
            base = GetEnvironmentPath("HOME", L"HOME") / ".config";
        fs::path folder = base / "crimson";
#endif

        std::error_code error;
        fs::create_directories(folder, error);
        if (error)
            LOG_WARN("[Editor] Can't create {}: {}", folder.string(), error.message());

        return folder;
    }

    fs::path GetDefaultProjectsFolder()
    {
#ifdef _WIN32
        const fs::path home = GetEnvironmentPath("USERPROFILE", L"USERPROFILE");
#else
        const fs::path home = GetEnvironmentPath("HOME", L"HOME");
#endif

        std::error_code error;
        const fs::path documents = home / "Documents";
        return fs::is_directory(documents, error) ? documents : home;
    }
}
