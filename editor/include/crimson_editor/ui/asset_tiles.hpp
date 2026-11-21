#pragma once
#include <string>

namespace crimson::editor::ui
{
    // The last part of an asset's path
    [[nodiscard]] std::string AssetFileName(const std::string& path);
}
