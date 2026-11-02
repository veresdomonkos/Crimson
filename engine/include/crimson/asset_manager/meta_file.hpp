#pragma once
#include <optional>
#include "crimson/asset_manager/asset_types.hpp"

namespace crimson::meta
{
    std::filesystem::path MetaPathFor(const std::filesystem::path& absoluteSource);
    std::optional<AssetInfo> Read(const std::filesystem::path& metaPath);
    bool Write(const std::filesystem::path& metaPath, const AssetInfo& meta);
}