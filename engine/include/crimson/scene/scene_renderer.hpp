#pragma once
#include <vector>

#include "crimson/renderer/frame_recorder.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_manager.hpp"
#include "crimson/renderer/shadow_atlas.hpp"
#include "crimson/scene/components.hpp"
#include "crimson/scene/entity.hpp"
#include "crimson/scene/tone_mapper.hpp"

namespace crimson
{
    class AssetManager;
    class Scene;

    // Loads the renderer's mesh and materials into Resolved and sizes MaterialAssets to the mesh's slots.
    // Empty slots get the material made for them (AssetManager::FindSlotMaterial), or else the default one.
    void ResolveMeshRenderer(MeshRendererComponent& renderer, AssetManager& assets);

    // Turns a scene into the frame's lighting data and render passes
    class SceneRenderer
    {
    public:
        SceneRenderer(GpuResourceManager& resources, Renderer& renderer, ShadowAtlasSettings atlas = {});
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        // Enables outlines on a target of this size. The outline material samples the outlined meshes' depth from its
        // OutlineMask texture, which is 1 where there are none.
        void SetOutlineMaterial(MaterialHandle outline, uint32_t width, uint32_t height);

        // Enables rendering the scene to an HDR target of this size, which the material maps to the main pass's target.
        // The material samples the scene from its SceneColor texture. The main pass's target then needs no depth buffer.
        void SetToneMapMaterial(MaterialHandle material, uint32_t width, uint32_t height);

        // Changes a material, so this has to run between frames
        void SetToneMapping(float exposure, ToneMapper mapper);

        [[nodiscard]] TextureHandle GetShadowAtlas() const { return m_shadowAtlas; }

        // Fills the frame's lighting, call before Record with the scene's world transforms up to date
        void BuildLighting(Scene& scene, FrameLighting& lighting);

        // Records one shadow pass per shadow view of the frame's lighting, then the main pass described by mainPass, then the tone mapping.
        // The main pass is named "Scene", and samples the shadow atlas besides what mainPass.Reads lists.
        // Clear colors are in linear light when tone mapping is enabled.
        // The outlined entity and its children get an outline, if there is one and outlines are enabled.
        void Record(FrameRecorder& frame, Scene& scene, const PassInfo& mainPass, Entity outlined = {});
    private:
        struct Drawable
        {
            const crimson::Mesh* Mesh = nullptr;
            ObjectId Object = 0;
            bool CastsShadows = false;
            bool Outlined = false;
        };

        void CreateScreenTriangle();
        void RecordOutlineMask(FrameRecorder& frame, const PassInfo& mainPass);

        GpuResourceManager& m_resources;
        ShadowAtlasSettings m_atlas;
        RenderTargetHandle m_shadowTarget;
        TextureHandle m_shadowAtlas;

        RenderTargetHandle m_hdrTarget;
        TextureHandle m_hdrColor;
        MaterialHandle m_toneMapMaterial;

        RenderTargetHandle m_outlineMaskTarget;
        TextureHandle m_outlineMask;
        MaterialHandle m_outlineMaterial;
        VertexBufferHandle m_screenTriangleVertices;
        IndexBufferHandle m_screenTriangleIndices;

        std::vector<Drawable> m_drawables;
        bool m_atlasFullLogged = false;
    };
}
