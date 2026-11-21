#include "crimson_editor/panels/asset_tiles.hpp"

namespace crimson::editor
{
    std::string AssetFileName(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
}
