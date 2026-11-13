#pragma once
#include <string_view>
#include <vector>

#include "crimson/asset_manager/asset_manager.hpp"

namespace crimson
{
    // What CreateSlotMaterials did
    struct SlotMaterials
    {
        std::vector<MaterialID> Created;
        size_t Kept = 0; // slots that had a material already, which is left as it is
    };

    // What a mesh's empty material slot uses when no material was made for it
    [[nodiscard]] MaterialID DefaultMaterial(const AssetManager& assets);

    // The material made for one of a mesh's slots: materials/<slot>.mat, next to the mesh. Invalid if there is none.
    [[nodiscard]] MaterialID FindSlotMaterial(const AssetManager& assets, MeshID mesh, std::string_view slot);

    // Makes the material each slot of an .obj looks for, materials/<slot>.mat next to it, from its .mtl: colors,
    // textures and shininess on the default shader, with sRGB turned off on its normal maps. A slot that has one
    // already keeps it, so running it again changes nothing. Reimports textures, so it has to run between frames.
    SlotMaterials CreateSlotMaterials(AssetManager& assets, MeshID mesh);
}
