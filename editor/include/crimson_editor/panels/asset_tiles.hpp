#pragma once
#include <string>

namespace crimson::editor
{
    // The last part of an asset's path
    [[nodiscard]] std::string AssetFileName(const std::string& path);
}
