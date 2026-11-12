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

        // xyz points along +U, and w is the side the bitangent (+V) is on, ±1 as mirrored UVs flip it.
        // A mesh without UVs has nothing to go by, and gets w = 0.
        glm::vec4 Tangent;
    };
    static_assert(sizeof(MeshVertex) == 48, "No padding, so vertices can be compared byte by byte");

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
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle  IndexBuffer;

        std::vector<SubMesh> SubMeshes;
        std::vector<MaterialHandle> Materials;
        std::vector<std::string> MaterialSlots; // names, in the same order as Materials
        MeshBounds Bounds;
    };
}