#pragma once
#include <string>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/editor_context.hpp"

namespace crimson::editor::ui
{
    // What a field shows for an asset: "wood.mat", "None (Texture)", or "Missing: textures/a.png" for one that isn't there
    [[nodiscard]] std::string AssetFieldText(const AssetManager& assets, AssetID id, AssetType type);

    // An object field: the asset's icon or thumbnail, its name, and a button that opens the picker. Assets of the type can
    // be dropped on it, and clicking the name shows the asset in the Assets panel. Returns true when another was picked
    // or dropped, even the one that an empty field shows. The picker's choice arrives on the frame after it was made, when the
    // field takes it. whenEmpty is what an empty field uses, which it shows in muted text, as it isn't chosen.
    bool AssetField(const char* id, EditorContext& context, AssetType type, AssetID& current, AssetID whenEmpty = {});

    // The same, for when the type is known when compiling
    template <typename T>
    bool AssetField(const char* id, EditorContext& context, TypedAssetID<T>& current)
    {
        AssetID picked = current;
        if (!AssetField(id, context, AssetType::Of<T>(), picked))
            return false;

        current = context.Session.GetAssets().As<T>(picked);
        return true;
    }
}
