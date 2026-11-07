#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "crimson/renderer/resource_handles.hpp"

namespace crimson
{
    struct MeshVertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 UV;
    };

    struct SubMesh
    {
        uint32_t FirstIndex   = 0;
        uint32_t IndexCount   = 0;
        uint32_t MaterialSlot = 0;
    };

    struct MeshBounds
    {
        glm::vec3 Min{ 0.0f };
        glm::vec3 Max{ 0.0f };
    };

    struct Mesh
    {
        VertexBufferHandle VB;
        IndexBufferHandle  IB;

        std::vector<SubMesh> SubMeshes;
        std::vector<MaterialHandle> Materials;
        std::vector<std::string> MaterialSlots; // names, in the same order as Materials

        // By slot, what draws it in a pass that only needs its shape, for a cutout material whose holes have to show
        // there too. Empty, or invalid for a slot, where the pass's own material does.
        std::vector<MaterialHandle> ShadowMaterials;
        std::vector<MaterialHandle> MaskMaterials;
        MeshBounds Bounds;
    };
}