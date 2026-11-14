#pragma once
#include <filesystem>

namespace crimson::platform
{
    // Hides a folder in the file manager. Names that start with a dot already are on Linux, so it only does something on Windows.
    void HideFolder(const std::filesystem::path& folder);
}
