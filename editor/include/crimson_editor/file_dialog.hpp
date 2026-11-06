#pragma once
#include <filesystem>
#include <optional>

namespace crimson
{
    class Window;
}

namespace crimson::editor
{
    // Shows the system's folder picker and blocks until it closes. Empty if it was cancelled or failed.
    [[nodiscard]] std::optional<std::filesystem::path> PickFolder(const Window& parent, const std::filesystem::path& startFolder);

    // Shows the system's picker for a program to run: an .exe, .cmd or .bat on Windows, any file elsewhere
    [[nodiscard]] std::optional<std::filesystem::path> PickProgram(const Window& parent, const std::filesystem::path& startFolder);
}
