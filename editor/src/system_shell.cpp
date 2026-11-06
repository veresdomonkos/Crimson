#include "crimson_editor/system_shell.hpp"

#include <array>
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

#ifdef _WIN32
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
