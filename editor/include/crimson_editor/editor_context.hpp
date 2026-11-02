#pragma once
#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/entity.hpp>

namespace crimson::editor
{
    // What the panels share
    struct EditorContext
    {
        Scene& ActiveScene;
        AssetManager& Assets;
        Entity Selected;
    };
}
