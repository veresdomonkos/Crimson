#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What differs between the operating systems. Each function has one definition per platform, in src/platform/windows/
// and src/platform/posix/, and CMake picks the files.
namespace crimson::editor::platform
{
    // An environment variable as a path, empty if it isn't set. Wide on Windows, where a user's folders can have non-ASCII names.
    [[nodiscard]] std::filesystem::path EnvironmentPath(const char* name);

    // Where programs keep their settings for this user, with Crimson's folder in it: %APPDATA%/Crimson, or
    // $XDG_CONFIG_HOME (else ~/.config)/crimson
    [[nodiscard]] std::filesystem::path UserConfigFolder();

    // The user's home folder: %USERPROFILE% or $HOME
    [[nodiscard]] std::filesystem::path HomeFolder();

    // A command line argument as a path. argv isn't UTF-8 on Windows, so it comes from the wide command line there.
    [[nodiscard]] std::filesystem::path CommandLineArgumentPath(int index, char* argv[]);

    // Where the code editors usually are on this system, by name, in the order they are offered. Some may not exist.
    [[nodiscard]] std::vector<std::pair<std::string, std::filesystem::path>> CodeEditorCandidates();

    // The extensions a program file has here, for the file picker's filter: "exe,cmd,bat" on Windows, empty elsewhere
    [[nodiscard]] std::string_view ProgramExtensions();
}
