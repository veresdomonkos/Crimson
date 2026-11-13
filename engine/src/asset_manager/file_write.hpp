#pragma once
#include <filesystem>
#include <string_view>

namespace crimson
{
    // Writes to a temporary file next to it and renames that over it, so a write that fails halfway doesn't take the old
    // file with it. Logs nothing, for the caller to say which asset it was.
    [[nodiscard]] bool WriteFileSafely(const std::filesystem::path& file, std::string_view contents);
}
