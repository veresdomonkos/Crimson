#include <tiny_obj_loader.h>

#include "crimson/asset_manager/importers/mesh_importer.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include "crimson/core/log.hpp"
#include "crimson/renderer/mesh.hpp"

#include "asset_manager/mtl_materials.hpp"
#include "mesh_tangents.hpp"

namespace crimson
{
    std::span<const std::string_view> MeshImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".obj" };
        return exts;
    }

    nlohmann::json MeshImporter::DefaultSettings() const
    {
        return { { "flipV", true } };
    }

    std::optional<Mesh> MeshImporter::Load(const AssetSource& source, ImportContext& ctx)
    {
        const std::string& name = source.Info.Path;

        const auto file = source.Read();
        if (!file)
            return {};

        std::istringstream stream(*file);
        MaterialFolderReader materialReader(source.File.parent_path());

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warning, error;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, &stream, &materialReader))
        {
            LOG_ERROR("[MeshImporter] '{}': {}", name, error);
            return {};
        }
        if (!warning.empty())
            LOG_WARN("[MeshImporter] '{}': {}", name, warning);

        const bool flipV = source.Info.Settings.value("flipV", true);

        Mesh mesh;

        for (const auto& m : materials)
            mesh.MaterialSlots.push_back(m.name);
        if (mesh.MaterialSlots.empty())
            mesh.MaterialSlots.push_back("default");

        mesh.Materials.resize(std::max<size_t>(1, materials.size()));

        // Every triangle's corners first, as the tangents depend on the triangles around a vertex. They are welded after.
        std::vector<MeshVertex> corners;
        std::vector<size_t> triangleSlots;

        for (const auto& shape : shapes)
        {
            const auto& idx = shape.mesh.indices;

            for (size_t face = 0; face < idx.size() / 3; ++face)
            {
                const int matId = shape.mesh.material_ids[face];
                const size_t slot = (matId >= 0 && static_cast<size_t>(matId) < mesh.Materials.size())
                                  ? static_cast<size_t>(matId) : 0;
                triangleSlots.push_back(slot);

                glm::vec3 faceNormal{ 0.0f, 1.0f, 0.0f };
                if (idx[face * 3].normal_index < 0)
                {
                    glm::vec3 p[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        const int vi = idx[face * 3 + k].vertex_index;
                        p[k] = { attrib.vertices[3 * vi], attrib.vertices[3 * vi + 1], attrib.vertices[3 * vi + 2] };
                    }
                    const glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
                    if (glm::dot(n, n) > 1e-12f) faceNormal = glm::normalize(n);
                }

                for (int k = 0; k < 3; ++k)
                {
                    const auto& i = idx[face * 3 + k];
                    const bool hasNormal = i.normal_index >= 0;

                    MeshVertex v{};
                    v.Position = { attrib.vertices[3 * i.vertex_index],
                                   attrib.vertices[3 * i.vertex_index + 1],
                                   attrib.vertices[3 * i.vertex_index + 2] };
                    v.Normal = hasNormal
                        ? glm::vec3{ attrib.normals[3 * i.normal_index],
                                     attrib.normals[3 * i.normal_index + 1],
                                     attrib.normals[3 * i.normal_index + 2] }
                        : faceNormal;
                    if (i.texcoord_index >= 0)
                    {
                        const float u = attrib.texcoords[2 * i.texcoord_index];
                        const float t = attrib.texcoords[2 * i.texcoord_index + 1];
                        v.UV = { u, flipV ? 1.0f - t : t };
                    }

                    corners.push_back(v);
                }
            }
        }

        if (corners.empty())
        {
            LOG_ERROR("[MeshImporter] '{}': no geometry", name);
            return {};
        }

        ComputeTangents(corners, !attrib.texcoords.empty());

        // Corners that came out the same are one vertex. Ones that only differ in their tangent, at a seam where the UVs
        // are mirrored, stay apart.
        std::vector<MeshVertex> vertices;
        std::vector<std::vector<uint32_t>> indicesBySlot(mesh.Materials.size());
        std::unordered_map<std::string_view, uint32_t> welded;
        welded.reserve(corners.size());

        glm::vec3 bmin{  std::numeric_limits<float>::max() };
        glm::vec3 bmax{ -std::numeric_limits<float>::max() };

        for (size_t corner = 0; corner < corners.size(); ++corner)
        {
            const MeshVertex& v = corners[corner];
            const std::string_view bytes(reinterpret_cast<const char*>(&v), sizeof(MeshVertex));

            const auto [it, added] = welded.try_emplace(bytes, static_cast<uint32_t>(vertices.size()));
            if (added)
            {
                vertices.push_back(v);
                bmin = glm::min(bmin, v.Position);
                bmax = glm::max(bmax, v.Position);
            }

            indicesBySlot[triangleSlots[corner / 3]].push_back(it->second);
        }

        std::vector<uint32_t> indices;
        for (size_t slot = 0; slot < indicesBySlot.size(); ++slot)
        {
            if (indicesBySlot[slot].empty()) continue;

            SubMesh sub;
            sub.FirstIndex   = static_cast<uint32_t>(indices.size());
            sub.IndexCount   = static_cast<uint32_t>(indicesBySlot[slot].size());
            sub.MaterialSlot = static_cast<uint32_t>(slot);
            mesh.SubMeshes.push_back(sub);

            indices.insert(indices.end(), indicesBySlot[slot].begin(), indicesBySlot[slot].end());
        }

        mesh.Bounds = { bmin, bmax };

        VertexBufferInfo vInfo{
            .Layout = { ShaderDataType::Float3, ShaderDataType::Float3, ShaderDataType::Float2, ShaderDataType::Float4 },
            .Size   = vertices.size() * sizeof(MeshVertex),
            .Usage  = BufferUsage::Static
        };
        IndexBufferInfo iInfo{
            .Size  = indices.size() * sizeof(uint32_t),
            .Usage = BufferUsage::Static,
            .Type  = IndexType::UInt32
        };

        mesh.VertexBuffer = ctx.Gpu.CreateVertexBuffer(vInfo, vertices.data());
        mesh.IndexBuffer = ctx.Gpu.CreateIndexBuffer(iInfo, indices.data());

        if (!mesh.VertexBuffer || !mesh.IndexBuffer)
        {
            LOG_ERROR("[MeshImporter] GPU buffer creation failed for '{}'", name);
            if (mesh.VertexBuffer) ctx.Gpu.DestroyVertexBuffer(mesh.VertexBuffer);
            if (mesh.IndexBuffer) ctx.Gpu.DestroyIndexBuffer(mesh.IndexBuffer);
            return {};
        }

        LOG_INFO("[MeshImporter] '{}': {} vertices, {} indices, {} submeshes",
                 name, vertices.size(), indices.size(), mesh.SubMeshes.size());
        return mesh;
    }

    void MeshImporter::Unload(Mesh& mesh, GpuResourceManager& gpu)
    {
        gpu.DestroyVertexBuffer(mesh.VertexBuffer);
        gpu.DestroyIndexBuffer(mesh.IndexBuffer);
    }
}