#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/asset_manager/importers/material_asset.hpp"
#include "crimson/asset_manager/importers/mesh_asset.hpp"
#include "crimson/renderer/frame_recorder.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_manager.hpp"
#include "crimson/renderer/shadow_atlas.hpp"
#include "crimson/scene/components.hpp"
#include "crimson/scene/entity.hpp"
#include "crimson/scene/tone_mapper.hpp"

namespace crimson
{
    class AssetManager; // only its name, as its header is heavy
    class Scene;

    // Turns a scene into the frame's lighting data and render passes
    class SceneRenderer
    {
    public:
        SceneRenderer(GpuResourceManager& resources, Renderer& renderer, AssetManager& assets, ShadowAtlasSettings atlas = {});
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        // Enables outlines on a target of this size. The outline material samples the outlined meshes' depth from its
        // OutlineMask texture, which is 1 where there are none.
        void SetOutlineMaterial(MaterialID outline, uint32_t width, uint32_t height);

        // Enables rendering the scene to an HDR target of this size, which the material maps to the main pass's target.
        // The material samples the scene from its SceneColor texture. The main pass's target then needs no depth buffer.
        void SetToneMapMaterial(MaterialID material, uint32_t width, uint32_t height);

        // Changes a material, so this has to run between frames
        void SetToneMapping(float exposure, ToneMapper mapper);

        [[nodiscard]] TextureHandle GetShadowAtlas() const { return m_shadowAtlas; }

        // Fills the frame's lighting, call before Record with the scene's world transforms up to date
        void BuildLighting(Scene& scene, FrameLighting& lighting);

        // Records one shadow pass that draws every shadow view of the frame's lighting into its tile of the atlas, if there
        // are any, then the main pass described by mainPass, then the tone mapping.
        // The main pass is named "Scene", and samples the shadow atlas besides what mainPass.Reads lists.
        // Clear colors are in linear light when tone mapping is enabled.
        // The outlined entity and its children get an outline, if there is one and outlines are enabled.
        // Looks up each renderer's mesh and materials, and the tone map and outline materials, so changes to them show.
        void Record(FrameRecorder& frame, Scene& scene, const PassInfo& mainPass, Entity outlined = {});
    private:
        // The materials of a drawable are m_materials[FirstMaterial, FirstMaterial + MaterialCount), as offsets since the vector grows meanwhile
        struct Drawable
        {
            const crimson::Mesh* Mesh = nullptr;
            uint32_t FirstMaterial = 0;
            uint32_t MaterialCount = 0;
            ObjectId Object = 0;
            bool CastsShadows = false;
            bool Outlined = false;
        };

        void CreateScreenTriangle();
        void SetUpToneMap(MaterialHandle material);
        void ApplyToneMapping(MaterialHandle material);
        void RecordShadows(FrameRecorder& frame);
        void RecordOutlineMask(FrameRecorder& frame, const PassInfo& mainPass);

        // What a mesh's empty slots use, worked out once per change of the assets, as it builds and hashes paths
        [[nodiscard]] const std::vector<MaterialID>& EmptySlotMaterials(MeshID mesh, const Mesh& loaded);

        [[nodiscard]] std::span<const MaterialHandle> MaterialsOf(const Drawable& drawable) const
        {
            return std::span(m_materials).subspan(drawable.FirstMaterial, drawable.MaterialCount);
        }

        GpuResourceManager& m_resources;
        AssetManager& m_assets;
        ShadowAtlasSettings m_atlas;
        RenderTargetHandle m_shadowTarget;
        TextureHandle m_shadowAtlas;

        // Held by id, and looked up every frame, so they follow the material being made again. What the handle it last
        // got set up on is, tells when it was.
        RenderTargetHandle m_hdrTarget;
        TextureHandle m_hdrColor;
        MaterialID m_toneMapMaterial;
        MaterialHandle m_toneMapSetUp;
        std::optional<std::pair<float, ToneMapper>> m_toneMapping; // as last set, for a new handle to get

        RenderTargetHandle m_outlineMaskTarget;
        TextureHandle m_outlineMask;
        MaterialID m_outlineMaterial;
        MaterialHandle m_outlineSetUp;
        VertexBufferHandle m_screenTriangleVertices;
        IndexBufferHandle m_screenTriangleIndices;

        std::vector<Drawable> m_drawables;
        std::vector<MaterialHandle> m_materials; // every drawable's, one per slot of its mesh
        std::unordered_map<AssetID, std::vector<MaterialID>> m_emptySlotMaterials; // by mesh
        MaterialID m_missingMaterial;
        std::optional<uint64_t> m_cachedVersion; // of the assets, when m_emptySlotMaterials and m_missingMaterial were worked out
        bool m_atlasFullLogged = false;
    };
}
