#pragma once
#include <vector>

#include "crimson/renderer/frame_context.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_manager.hpp"
#include "crimson/renderer/shadow_atlas.hpp"
#include "crimson/scene/components.hpp"

namespace crimson
{
    class AssetManager;
    class Scene;

    // Loads the renderer's mesh and materials into Resolved and sizes MaterialAssets to the mesh's slots.
    // Empty slots get defaultMaterial.
    void ResolveMeshRenderer(MeshRendererComponent& renderer, AssetManager& assets, AssetID defaultMaterial);

    // Turns a scene into the frame's lighting data and render passes
    class SceneRenderer
    {
    public:
        SceneRenderer(GpuResourceManager& resources, Renderer& renderer, ShadowAtlasSettings atlas = {});
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        void SetShadowMaterial(MaterialHandle material) { m_shadowMaterial = material; }
        [[nodiscard]] TextureHandle GetShadowAtlas() const { return m_shadowAtlas; }

        // Call before Renderer::BeginFrame, with the scene's world transforms up to date
        void BuildLighting(Scene& scene, FrameLightingData& lighting);

        // Records one shadow pass per shadow view, then the main pass described by mainPass
        void RecordPasses(FrameContext& frame, Scene& scene, const FrameLightingData& lighting, const RenderPassInfo& mainPass);
    private:
        struct Drawable
        {
            const Mesh* Mesh = nullptr;
            ObjectId Object = 0;
            bool CastsShadows = false;
        };

        GpuResourceManager& m_resources;
        ShadowAtlasSettings m_atlas;
        RenderTargetHandle m_shadowTarget;
        TextureHandle m_shadowAtlas;
        MaterialHandle m_shadowMaterial;

        std::vector<Drawable> m_drawables;
        bool m_atlasFullLogged = false;
    };
}
