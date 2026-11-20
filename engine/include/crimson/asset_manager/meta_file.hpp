#pragma once
#include <optional>
#include "crimson/asset_manager/asset_types.hpp"

namespace crimson::meta
{
    std::filesystem::path PathFor(const std::filesystem::path& absoluteSource);
    // The type its importer makes is the file's type, whatever the meta says. Nothing if it can't be read.
    std::optional<AssetInfo> Read(const std::filesystem::path& metaPath, AssetType importerType);
    bool Write(const std::filesystem::path& metaPath, const AssetInfo& meta);
}
