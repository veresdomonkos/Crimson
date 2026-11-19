#pragma once
#include <string_view>
#include <vector>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/asset_manager/importers/material_asset.hpp"
#include "crimson/asset_manager/importers/mesh_asset.hpp"

namespace crimson
{
    // What slot_materials::Create did
    struct SlotMaterials
    {
        std::vector<MaterialID> Created;
        size_t Kept = 0; // slots that had a material already, which is left as it is
    };

    namespace slot_materials
    {
        // What a mesh's empty material slot uses when no material was made for it
        [[nodiscard]] MaterialID DefaultMaterial(const AssetManager& assets);

        // The material made for one of a mesh's slots: materials/<slot>.mat, next to the mesh. Invalid if there is none.
        [[nodiscard]] MaterialID Find(const AssetManager& assets, MeshID mesh, std::string_view slot);

        // What a renderer's empty slot uses: the material made for the slot if there is one, else the default
        [[nodiscard]] MaterialID ForEmptySlot(const AssetManager& assets, MeshID mesh, std::string_view slot);

        // What a slot draws with when the material chosen for it is missing: a plain magenta one, to be seen
        [[nodiscard]] MaterialID MissingMaterial(const AssetManager& assets);

        // Makes the material each slot of an .obj looks for, materials/<slot>.mat next to it, from its .mtl: colors,
        // textures and shininess on the default shader, with sRGB turned off on its normal maps. A slot that has one
        // already keeps it, so running it again changes nothing. Reimports textures, so it has to run between frames.
        SlotMaterials Create(AssetManager& assets, MeshID mesh);
    }
}
