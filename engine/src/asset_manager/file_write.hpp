#pragma once
#include <filesystem>
#include <string_view>

namespace crimson
{
    // Writes to a temporary file next to it and renames that over it, so a write that fails halfway doesn't take the old
    // file with it. Logs nothing, for the caller to say which asset it was. Writers that can share a file pass a suffix
    // of their own, so that they never share the temporary one.
    [[nodiscard]] bool WriteFileSafely(const std::filesystem::path& file, std::string_view contents,
                                       std::string_view temporarySuffix = ".saving");
}
