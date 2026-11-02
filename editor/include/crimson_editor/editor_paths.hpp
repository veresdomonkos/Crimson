#pragma once
#include <filesystem>

namespace crimson::editor
{
    // Where the editor keeps its own settings, for every project. Created if it doesn't exist.
    [[nodiscard]] std::filesystem::path GetUserConfigFolder();

    // Where the file dialogs start when there is nothing better to go on
    [[nodiscard]] std::filesystem::path GetDefaultProjectsFolder();
}
