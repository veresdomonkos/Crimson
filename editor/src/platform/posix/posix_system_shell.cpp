#include "crimson_editor/system_shell.hpp"

#include <system_error>

#include <crimson/core/log.hpp>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace crimson::editor
{
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
}
