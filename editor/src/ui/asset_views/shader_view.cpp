#include "crimson_editor/ui/asset_type_registry.hpp"

#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    EditorAssetType ShaderAssetType()
    {
        return {
            .Type = AssetType::Shader,
            .Name = "Shader",
            .Icon = icons::kShader,
            .Color = ImVec4(0.52f, 0.46f, 0.70f, 1.00f)
        };
    }
}
