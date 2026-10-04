#include <tiny_obj_loader.h>

#include "crimson/asset_manager/importers/mesh_importer.hpp"

#include <array>
#include <map>
#include <algorithm>
#include "crimson/core/log.hpp"
#include "crimson/renderer/mesh.hpp"

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

    LoadedAsset MeshImporter::Load(const AssetMetadata& meta, const std::filesystem::path& path, ImportContext& ctx)
    {
        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        config.mtl_search_path = path.parent_path().string();

        tinyobj::ObjReader reader;
        if (!reader.ParseFromFile(path.string(), config))
        {
            LOG_ERROR("[MeshImporter] '{}': {}", path.string(), reader.Error());
            return {};
        }
        if (!reader.Warning().empty())
            LOG_WARN("[MeshImporter] '{}': {}", path.string(), reader.Warning());

        const auto& attrib    = reader.GetAttrib();
        const auto& shapes    = reader.GetShapes();
        const auto& materials = reader.GetMaterials();
        const bool flipV      = meta.Settings.value("flipV", true);

        Mesh mesh;

        /*
        for (const auto& m : materials)
            mesh.MaterialSlots.push_back(m.name);
        if (mesh.MaterialSlots.empty())
            mesh.MaterialSlots.push_back("default");
        */

        mesh.Materials.resize(std::max<size_t>(1, materials.size()));

        std::vector<MeshVertex> vertices;
        std::vector<std::vector<uint32_t>> indicesBySlot(mesh.Materials.size());
        std::map<std::array<int, 3>, uint32_t> dedup;

        glm::vec3 bmin{  std::numeric_limits<float>::max() };
        glm::vec3 bmax{ -std::numeric_limits<float>::max() };

        for (const auto& shape : shapes)
        {
            const auto& idx = shape.mesh.indices;

            for (size_t face = 0; face < idx.size() / 3; ++face)
            {
                const int matId = shape.mesh.material_ids[face];
                const size_t slot = (matId >= 0 && static_cast<size_t>(matId) < indicesBySlot.size())
                                  ? static_cast<size_t>(matId) : 0;

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
                    const std::array<int, 3> key{ i.vertex_index, i.normal_index, i.texcoord_index };

                    if (hasNormal)
                    {
                        if (auto it = dedup.find(key); it != dedup.end())
                        {
                            indicesBySlot[slot].push_back(it->second);
                            continue;
                        }
                    }

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

                    bmin = glm::min(bmin, v.Position);
                    bmax = glm::max(bmax, v.Position);

                    const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(v);
                    if (hasNormal) dedup.emplace(key, newIndex);
                    indicesBySlot[slot].push_back(newIndex);
                }
            }
        }

        if (vertices.empty())
        {
            LOG_ERROR("[MeshImporter] '{}': no geometry", path.string());
            return {};
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
            .Layout = { ShaderDataType::Float3, ShaderDataType::Float3, ShaderDataType::Float2 },
            .Size   = vertices.size() * sizeof(MeshVertex),
            .Usage  = BufferUsage::Static
        };
        IndexBufferInfo iInfo{
            .Size  = indices.size() * sizeof(uint32_t),
            .Usage = BufferUsage::Static,
            .Type  = IndexType::UInt32
        };

        mesh.VB = ctx.Gpu.CreateVertexBuffer(vInfo, vertices.data());
        mesh.IB = ctx.Gpu.CreateIndexBuffer(iInfo, indices.data());

        if (!mesh.VB || !mesh.IB)
        {
            LOG_ERROR("[MeshImporter] GPU buffer creation failed for '{}'", path.string());
            if (mesh.VB) ctx.Gpu.DestroyVertexBuffer(mesh.VB);
            if (mesh.IB) ctx.Gpu.DestroyIndexBuffer(mesh.IB);
            return {};
        }

        LOG_INFO("[MeshImporter] '{}': {} vertices, {} indices, {} submeshes",
                 path.filename().string(), vertices.size(), indices.size(), mesh.SubMeshes.size());
        return mesh;
    }

    void MeshImporter::Unload(const LoadedAsset& asset, GpuResourceManager& gpu)
    {
        if (const auto* mesh = std::get_if<Mesh>(&asset))
        {
            gpu.DestroyVertexBuffer(mesh->VB);
            gpu.DestroyIndexBuffer(mesh->IB);
        }
    }
}