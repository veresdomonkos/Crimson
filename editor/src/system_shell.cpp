#include "crimson_editor/system_shell.hpp"

#include <array>
#include <cwctype>
#include <system_error>
#include <vector>

#include <crimson/core/log.hpp>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
    #include <shlobj.h>
#else
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace crimson::editor
{
    std::string FileUri(const fs::path& path)
    {
        static constexpr std::array<char, 16> kHex = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

        const std::u8string utf8 = path.generic_u8string();

        // A path with a drive letter, such as C:/, still needs the slash that starts the path part of the URI
        std::string uri = "file://";
        if (utf8.empty() || utf8.front() != u8'/')
            uri += '/';

        for (const char8_t c : utf8)
        {
            const auto byte = static_cast<unsigned char>(c);
            const bool plain = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
                || byte == '-' || byte == '.' || byte == '_' || byte == '~' || byte == '/';

            if (plain)
            {
                uri += static_cast<char>(byte);
            }
            else
            {
                uri += '%';
                uri += kHex[byte >> 4];
                uri += kHex[byte & 0x0F];
            }
        }

        return uri;
    }

    std::string PathToUtf8(const fs::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    // How CommandLineToArgvW reads a command line back: backslashes are only special in front of a quote
    std::string QuoteWindowsArgument(const std::string& argument)
    {
        if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos)
            return argument;

        std::string quoted = "\"";
        size_t backslashes = 0;

        for (const char c : argument)
        {
            if (c == '\\')
            {
                ++backslashes;
                continue;
            }

            // Backslashes in front of a quote are doubled, and the quote gets one more to be taken as it is
            if (c == '"')
                quoted.append(backslashes * 2 + 1, '\\');
            else
                quoted.append(backslashes, '\\');

            quoted += c;
            backslashes = 0;
        }

        // Backslashes at the end come before the closing quote, so they are doubled too
        quoted.append(backslashes * 2, '\\');
        quoted += '"';
        return quoted;
    }

#ifdef _WIN32
    namespace
    {
        std::wstring Widen(const std::string& text)
        {
            if (text.empty())
                return {};

            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }
    }

    bool StartProgram(const fs::path& program, const std::vector<std::string>& arguments)
    {
        std::string commandLine = QuoteWindowsArgument(PathToUtf8(program));
        for (const std::string& argument : arguments)
            commandLine += " " + QuoteWindowsArgument(argument);

        // A batch file, such as VS Code's code.cmd, only runs through cmd, which then shouldn't show a window
        std::wstring extension = program.extension().wstring();
        for (wchar_t& c : extension)
            c = static_cast<wchar_t>(towlower(c));

        const bool batch = extension == L".cmd" || extension == L".bat";
        if (batch)
            commandLine = "cmd.exe /d /s /c \"" + commandLine + "\"";

        std::wstring wideCommandLine = Widen(commandLine);
        const std::wstring application = batch ? std::wstring() : program.wstring();

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};

        const BOOL started = CreateProcessW(batch ? nullptr : application.c_str(), wideCommandLine.data(), nullptr, nullptr, FALSE,
            batch ? CREATE_NO_WINDOW : 0, nullptr, nullptr, &startup, &process);

        if (!started)
        {
            LOG_ERROR("[Editor] Can't start {} (error {})", program.string(), static_cast<unsigned long>(GetLastError()));
            return false;
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    bool OpenInDefaultApp(const fs::path& path)
    {
        const std::wstring file = path.wstring();
        const std::wstring folder = path.parent_path().wstring();

        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, folder.c_str(), SW_SHOWNORMAL));
        if (result > 32)
            return true;

        // Nothing is set up for this type of file, so Windows asks what to open it with, as Explorer does
        if (result == SE_ERR_NOASSOC)
        {
            OPENASINFO info{};
            info.pcszFile = file.c_str();
            info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC;

            if (SUCCEEDED(SHOpenWithDialog(nullptr, &info)))
                return true;
        }

        LOG_ERROR("[Editor] Can't open {} (error {})", path.string(), static_cast<long long>(result));
        return false;
    }

    bool RevealInFileManager(const fs::path& path)
    {
        // The shell's folder views need COM. It may already be set up on this thread, in another mode, which is fine.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        bool revealed = false;

        if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.wstring().c_str()))
        {
            revealed = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
            ILFree(item);
        }

        if (SUCCEEDED(com))
            CoUninitialize();

        if (!revealed)
            LOG_ERROR("[Editor] Can't show {} in Explorer", path.string());

        return revealed;
    }

    const char* RevealInFileManagerLabel()
    {
        return "Show in Explorer";
    }
#else
    namespace
    {
        // Runs a program. Waiting gives whether it succeeded. Without waiting it is started through a second fork,
        // so it runs on its own, and its ending leaves nothing behind for the editor to collect.
        bool Run(const std::vector<std::string>& command, bool wait)
        {
            std::vector<char*> argv;
            for (const std::string& argument : command)
                argv.push_back(const_cast<char*>(argument.c_str()));
            argv.push_back(nullptr);

            const pid_t child = fork();
            if (child < 0)
                return false;

            if (child == 0)
            {
                if (!wait)
                {
                    const pid_t grandchild = fork();
                    if (grandchild != 0)
                        _exit(grandchild < 0 ? 1 : 0);

                    setsid();
                }

                // What it prints isn't for the editor's output
                const int null = open("/dev/null", O_RDWR);
                if (null >= 0)
                {
                    dup2(null, STDOUT_FILENO);
                    dup2(null, STDERR_FILENO);
                }

                execvp(argv[0], argv.data());
                _exit(127);
            }

            int status = 0;
            if (waitpid(child, &status, 0) < 0)
                return false;

            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
    }

    bool StartProgram(const fs::path& program, const std::vector<std::string>& arguments)
    {
        std::vector<std::string> command = { PathToUtf8(program) };
        command.insert(command.end(), arguments.begin(), arguments.end());

        // Without waiting, the program not existing only shows in the child, so it is checked first
        std::error_code error;
        const bool found = program.has_parent_path() ? fs::exists(program, error) : true;

        if (!found || !Run(command, false))
        {
            LOG_ERROR("[Editor] Can't start {}", program.string());
            return false;
        }

        return true;
    }

    bool OpenInDefaultApp(const fs::path& path)
    {
    #ifdef __APPLE__
        const bool opened = Run({ "open", path.string() }, false);
    #else
        const bool opened = Run({ "xdg-open", path.string() }, false);
    #endif

        if (!opened)
            LOG_ERROR("[Editor] Can't open {}", path.string());

        return opened;
    }

    bool RevealInFileManager(const fs::path& path)
    {
    #ifdef __APPLE__
        const bool revealed = Run({ "open", "-R", path.string() }, false);
    #else
        // The file managers that know this call, such as Nautilus and Dolphin, select the file. The others open its folder.
        const bool revealed = Run({ "dbus-send", "--session", "--print-reply", "--dest=org.freedesktop.FileManager1", "--type=method_call",
                                    "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems",
                                    "array:string:" + FileUri(path), "string:" }, true)
            || Run({ "xdg-open", path.parent_path().string() }, false);
    #endif

        if (!revealed)
            LOG_ERROR("[Editor] Can't show {} in the file manager", path.string());

        return revealed;
    }

    const char* RevealInFileManagerLabel()
    {
    #ifdef __APPLE__
        return "Reveal in Finder";
    #else
        return "Show in File Manager";
    #endif
    }
#endif
}
