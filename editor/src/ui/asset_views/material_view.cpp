#include "crimson_editor/ui/asset_type_registry.hpp"

#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    EditorAssetType MaterialAssetType()
    {
        return {
            .Type = AssetType::Material,
            .Name = "Material",
            .Icon = icons::kMaterial,
            .Color = ImVec4(0.66f, 0.42f, 0.52f, 1.00f)
        };
    }
}
