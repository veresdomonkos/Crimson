#include "crimson_editor/ui/asset_type_registry.hpp"

#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    EditorAssetType MeshAssetType()
    {
        return {
            .Type = AssetType::Mesh,
            .Name = "Mesh",
            .Icon = icons::kMesh,
            .Color = ImVec4(0.38f, 0.54f, 0.68f, 1.00f)
        };
    }
}
