#include "crimson/asset_manager/importers/scene_importer.hpp"

namespace crimson
{
    std::span<const std::string_view> SceneImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".scene" };
        return exts;
    }
}
