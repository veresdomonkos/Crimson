#include "crimson_editor/file_dialog.hpp"

#include <string>

#include <crimson/core/log.hpp>
#include <crimson/core/window.hpp>
#include <nfd.h>

#include "crimson_editor/platform.hpp"

namespace crimson::editor
{
    // The dialog is a child of the window, so it stays in front of it
    static nfdwindowhandle_t ParentOf(const Window& window)
    {
        const OSWindowHandle handle = window.GetOSHandle();

        nfdwindowhandle_t parent{};
        switch (handle.Type)
        {
            case OSWindowHandle::Kind::Win32: parent.type = NFD_WINDOW_HANDLE_TYPE_WINDOWS; break;
            case OSWindowHandle::Kind::X11:   parent.type = NFD_WINDOW_HANDLE_TYPE_X11; break;
            case OSWindowHandle::Kind::Cocoa: parent.type = NFD_WINDOW_HANDLE_TYPE_COCOA; break;
        }
        parent.handle = reinterpret_cast<void*>(handle.Value);
        return parent;
    }

    std::optional<std::filesystem::path> PickFolder(const Window& parent, const std::filesystem::path& startFolder)
    {
        const std::string start = startFolder.string();

        nfdpickfolderu8args_t args{};
        args.defaultPath = start.empty() ? nullptr : start.c_str();
        args.parentWindow = ParentOf(parent);

        nfdu8char_t* picked = nullptr;
        const nfdresult_t result = NFD_PickFolderU8_With(&picked, &args);

        if (result == NFD_ERROR)
            LOG_ERROR("[Editor] The folder picker failed: {}", NFD_GetError());

        if (result != NFD_OKAY)
            return std::nullopt;

        std::filesystem::path path(picked);
        NFD_FreePathU8(picked);
        return path;
    }

    std::optional<std::filesystem::path> PickProgram(const Window& parent, const std::filesystem::path& startFolder)
    {
        const std::string start = startFolder.string();

        nfdopendialogu8args_t args{};
        args.defaultPath = start.empty() ? nullptr : start.c_str();
        args.parentWindow = ParentOf(parent);

        const std::string extensions(platform::ProgramExtensions());
        const nfdu8filteritem_t filters[] = { { "Programs", extensions.c_str() } };
        if (!extensions.empty())
        {
            args.filterList = filters;
            args.filterCount = 1;
        }

        nfdu8char_t* picked = nullptr;
        const nfdresult_t result = NFD_OpenDialogU8_With(&picked, &args);

        if (result == NFD_ERROR)
            LOG_ERROR("[Editor] The file picker failed: {}", NFD_GetError());

        if (result != NFD_OKAY)
            return std::nullopt;

        // nfd gives UTF-8, which the path takes as it is
        const std::string text(picked);
        NFD_FreePathU8(picked);
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
}
