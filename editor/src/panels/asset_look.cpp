#include "crimson_editor/panels/asset_look.hpp"

#include <algorithm>

#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/mesh_asset.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    ui::Color AssetColor(AssetType type)
    {
        if (type.Is<MaterialAsset>()) return ui::theme::AssetMaterial;
        if (type.Is<MeshAsset>())     return ui::theme::AssetMesh;
        if (type.Is<ShaderAsset>())   return ui::theme::AssetShader;
        if (type.Is<TextureAsset>())  return ui::theme::AssetTexture;
        if (type.Is<SceneAsset>())    return ui::theme::AssetScene;

        return ui::theme::AssetOther;
    }

    const char* AssetIcon(AssetType type)
    {
        if (type.Is<MeshAsset>())     return ui::icons::kMesh;
        if (type.Is<MaterialAsset>()) return ui::icons::kMaterial;
        if (type.Is<ShaderAsset>())   return ui::icons::kShader;
        if (type.Is<TextureAsset>())  return ui::icons::kTexture;
        if (type.Is<SceneAsset>())    return ui::icons::kScene;

        return ui::icons::kFile;
    }

    void DrawAssetPicture(ui::Painter& painter, const ui::Rect& icon, AssetType type, TextureHandle picture)
    {
        if (!picture)
        {
            painter.Icon(icon, AssetIcon(type), AssetColor(type));
            return;
        }

        const glm::vec2 size = icon.Size();
        const float side = std::min(size.x, size.y);
        const glm::vec2 center = icon.Center();

        painter.Image(picture, ui::Rect{ center - glm::vec2(side * 0.5f), center + glm::vec2(side * 0.5f) });
    }
}
