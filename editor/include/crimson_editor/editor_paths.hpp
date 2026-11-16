#pragma once
#include <filesystem>

namespace crimson::editor
{
    // Where the editor keeps its own settings, for every project. Created if it doesn't exist.
    [[nodiscard]] std::filesystem::path GetUserConfigFolder();

    // Where what importing the engine's own assets makes is kept, for every project: CRIMSON_CACHE if it is set, or the
    // user's cache folder. Not created, the cache does that when it writes.
    [[nodiscard]] std::filesystem::path GetEngineCacheFolder();

    // Where the file dialogs start when there is nothing better to go on
    [[nodiscard]] std::filesystem::path GetDefaultProjectsFolder();
}
