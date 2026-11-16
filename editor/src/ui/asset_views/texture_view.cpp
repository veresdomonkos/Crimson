#include "crimson_editor/ui/asset_type_registry.hpp"

#include "crimson_editor/asset_thumbnails.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    EditorAssetType TextureAssetType()
    {
        return {
            .Type = AssetType::Texture,
            .Name = "Texture",
            .Icon = icons::kTexture,
            .Color = ImVec4(0.40f, 0.62f, 0.48f, 1.00f),
            .Thumbnail = [](const AssetManager& assets, AssetThumbnails& thumbnails, AssetID id)
            {
                return thumbnails.Get(assets.As<AssetType::Texture>(id));
            }
        };
    }
}
