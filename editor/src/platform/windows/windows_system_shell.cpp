#include "crimson_editor/system_shell.hpp"

#include <cwctype>
#include <system_error>

#include <crimson/core/log.hpp>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace crimson::editor
{
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
}
