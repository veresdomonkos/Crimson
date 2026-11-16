#include "crimson_editor/ui/asset_type_registry.hpp"

#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    EditorAssetType SceneAssetType()
    {
        return {
            .Type = AssetType::Scene,
            .Name = "Scene",
            .Icon = icons::kScene,
            .Color = ImVec4(0.70f, 0.50f, 0.36f, 1.00f)
        };
    }
}
