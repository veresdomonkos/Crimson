#include "crimson_editor/file_dialog.hpp"

#include <string>

#include <crimson/core/log.hpp>
#include <crimson/core/window.hpp>
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <nfd_glfw3.h>

namespace crimson::editor
{
    std::optional<std::filesystem::path> PickFolder(const Window& parent, const std::filesystem::path& startFolder)
    {
        const std::string start = startFolder.string();

        nfdpickfolderu8args_t args{};
        args.defaultPath = start.empty() ? nullptr : start.c_str();
        NFD_GetNativeWindowFromGLFWWindow(static_cast<GLFWwindow*>(parent.GetNativeHandle()), &args.parentWindow);

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
        NFD_GetNativeWindowFromGLFWWindow(static_cast<GLFWwindow*>(parent.GetNativeHandle()), &args.parentWindow);

#ifdef _WIN32
        const nfdu8filteritem_t filters[] = { { "Programs", "exe,cmd,bat" } };
        args.filterList = filters;
        args.filterCount = 1;
#endif

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
