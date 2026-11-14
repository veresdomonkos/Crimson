#include "crimson_editor/platform.hpp"

#include <cstdlib>
#include <string>
#include <system_error>

#include <windows.h>
#include <shellapi.h>

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
    }

    fs::path EnvironmentPath(const char* name)
    {
        const std::wstring wideName(name, name + std::char_traits<char>::length(name));
        const wchar_t* value = _wgetenv(wideName.c_str());
        return value ? fs::path(value) : fs::path();
    }

    fs::path UserConfigFolder()
    {
        return EnvironmentPath("APPDATA") / "Crimson";
    }

    fs::path HomeFolder()
    {
        return EnvironmentPath("USERPROFILE");
    }

    fs::path CommandLineArgumentPath(int index, char* [])
    {
        // argv isn't UTF-8 on Windows, so a path with non-ASCII characters has to come from the wide command line
        int count = 0;
        wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &count);
        fs::path path = wideArguments && index < count ? fs::path(wideArguments[index]) : fs::path();
        LocalFree(wideArguments);
        return path;
    }

    std::vector<std::pair<std::string, fs::path>> CodeEditorCandidates()
    {
        std::vector<std::pair<std::string, fs::path>> candidates;
        std::error_code error;

        const fs::path localPrograms = EnvironmentPath("LOCALAPPDATA") / "Programs";
        const std::vector<fs::path> programFiles = { EnvironmentPath("ProgramFiles"), EnvironmentPath("ProgramFiles(x86)") };

        // Its command line, bin\code.cmd, is what takes -g file:line, so that comes before Code.exe itself
        for (const fs::path& install : { localPrograms / "Microsoft VS Code", programFiles[0] / "Microsoft VS Code", programFiles[1] / "Microsoft VS Code" })
        {
            const fs::path commandLine = install / "bin" / "code.cmd";
            candidates.emplace_back("VS Code", IsFile(commandLine) ? commandLine : install / "Code.exe");
        }

        // Visual Studio is in a folder for each version, then one for each edition
        for (const fs::path& root : programFiles)
        {
            const fs::path studio = root / "Microsoft Visual Studio";

            for (fs::directory_iterator version(studio, error), end; !error && version != end; version.increment(error))
            {
                for (fs::directory_iterator edition(version->path(), error); !error && edition != end; edition.increment(error))
                {
                    candidates.emplace_back("Visual Studio " + version->path().filename().string() + " " + edition->path().filename().string(),
                                            edition->path() / "Common7" / "IDE" / "devenv.exe");
                }

                error.clear();
            }

            error.clear();
        }

        // JetBrains installs a folder for each version, and the Toolbox puts them in the user's programs
        for (const fs::path& root : { programFiles[0] / "JetBrains", localPrograms })
        {
            for (fs::directory_iterator product(root, error), end; !error && product != end; product.increment(error))
            {
                candidates.emplace_back(product->path().filename().string(), product->path() / "bin" / "rider64.exe");
                candidates.emplace_back(product->path().filename().string(), product->path() / "bin" / "clion64.exe");
            }

            error.clear();
        }

        return candidates;
    }

    std::string_view ProgramExtensions()
    {
        return "exe,cmd,bat";
    }
}
