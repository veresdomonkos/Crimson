#include "crimson_editor/platform.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace crimson::editor::platform
{
    namespace
    {
        bool IsFile(const fs::path& path)
        {
            std::error_code error;
            return fs::is_regular_file(path, error);
        }

        // Where a program is on the PATH, empty if it isn't there
        fs::path FindOnPath(const std::string& program)
        {
            const std::string path = EnvironmentPath("PATH").string();

            for (size_t start = 0; start <= path.size();)
            {
                const size_t end = std::min(path.find(':', start), path.size());
                if (end > start)
                {
                    const fs::path candidate = fs::path(path.substr(start, end - start)) / program;
                    if (IsFile(candidate))
                        return candidate;
                }

                start = end + 1;
            }

            return {};
        }
    }

    fs::path EnvironmentPath(const char* name)
    {
        const char* value = std::getenv(name);
        return value ? fs::path(value) : fs::path();
    }

    fs::path UserConfigFolder()
    {
        fs::path base = EnvironmentPath("XDG_CONFIG_HOME");
        if (base.empty())
            base = EnvironmentPath("HOME") / ".config";

        return base / "crimson";
    }

    fs::path HomeFolder()
    {
        return EnvironmentPath("HOME");
    }

    fs::path CommandLineArgumentPath(int index, char* argv[])
    {
        return argv[index];
    }

    std::vector<std::pair<std::string, fs::path>> CodeEditorCandidates()
    {
        const std::pair<const char*, const char*> onPath[] = {
            { "VS Code", "code" }, { "VSCodium", "codium" }, { "Rider", "rider" }, { "CLion", "clion" }
        };

        std::vector<std::pair<std::string, fs::path>> candidates;
        for (const auto& [name, program] : onPath)
            candidates.emplace_back(name, FindOnPath(program));

    #ifdef __APPLE__
        candidates.emplace_back("VS Code", "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
    #endif

        return candidates;
    }

    std::string_view ProgramExtensions()
    {
        return {};
    }
}
