#include "platform/files.hpp"

#include <windows.h>

namespace crimson::platform
{
    void HideFolder(const std::filesystem::path& folder)
    {
        // Also for a folder made before it was hidden
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_HIDDEN))
            SetFileAttributesW(folder.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
    }
}
