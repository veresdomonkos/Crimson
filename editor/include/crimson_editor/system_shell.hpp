#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace crimson::editor
{
    // Opens a file in the program the system has for its type, or a folder in the file manager. Doesn't wait for it.
    // On Windows a file type with no program set up asks for one. Returns false, after logging why, if it can't.
    bool OpenInDefaultApp(const std::filesystem::path& path);

    // Opens the file manager at what holds a file or folder, with it selected
    bool RevealInFileManager(const std::filesystem::path& path);

    // What the file manager is called here, for the menu item that reveals a file in it
    const char* RevealInFileManagerLabel();

    // A path as a file:// URI, for the Linux file managers. Only letters, digits, - . _ ~ and / are written as they are.
    std::string FileUri(const std::filesystem::path& path);

    // Starts a program with these arguments, each passed on as one, and doesn't wait for it. The arguments are UTF-8.
    // Returns false, after logging why, if it can't be started.
    bool StartProgram(const std::filesystem::path& program, const std::vector<std::string>& arguments);

    // One argument for a Windows command line, quoted so the program reads it back as it was, spaces, quotes and
    // backslashes included
    std::string QuoteWindowsArgument(const std::string& argument);

    // A path's text as UTF-8, whatever the platform keeps paths in
    std::string PathToUtf8(const std::filesystem::path& path);
}
