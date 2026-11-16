#pragma once
#include <filesystem>

namespace crimson::editor
{
    // Where the editor keeps its own settings, for every project. Created if it doesn't exist.
    [[nodiscard]] std::filesystem::path GetUserConfigFolder();

    // Where the built-in shaders are kept once compiled, for every project: CRIMSON_SHADER_CACHE if it is set, or a folder
    // in the user's cache folder. Not created, the cache does that when it writes.
    [[nodiscard]] std::filesystem::path GetShaderCacheFolder();

    // Where the file dialogs start when there is nothing better to go on
    [[nodiscard]] std::filesystem::path GetDefaultProjectsFolder();
}
