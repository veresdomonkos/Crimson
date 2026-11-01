#include "crimson/scene/scene_renderer.hpp"

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    void ResolveMeshRenderer(MeshRendererComponent& renderer, AssetManager& assets, AssetID defaultMaterial)
    {
        renderer.Resolved = {};

        if (!renderer.MeshAsset)
            return;

        auto mesh = assets.GetAsset<Mesh>(renderer.MeshAsset);
        if (!mesh)
            return;

        renderer.Resolved = std::move(*mesh);
        renderer.MaterialAssets.resize(renderer.Resolved.Materials.size());

        for (size_t slot = 0; slot < renderer.MaterialAssets.size(); ++slot)
        {
            AssetID& asset = renderer.MaterialAssets[slot];
            if (!asset)
                asset = defaultMaterial;

            renderer.Resolved.Materials[slot] = asset
                ? assets.GetAsset<MaterialHandle>(asset).value_or(MaterialHandle::Invalid())
                : MaterialHandle::Invalid();
        }
    }

    SceneRenderer::SceneRenderer(GpuResourceManager& resources, Renderer& renderer, ShadowAtlasSettings atlas)
        : m_resources(resources), m_atlas(atlas)
    {
        const RenderTargetInfo info{ .Width = atlas.AtlasSize, .Height = atlas.AtlasSize, .DepthFormat = TextureFormat::Depth32F };
        m_shadowTarget = resources.CreateRenderTarget(info);
        m_shadowAtlas = resources.GetDepthAttachment(m_shadowTarget).value();

        renderer.SetShadowMap(m_shadowAtlas);
    }

    SceneRenderer::~SceneRenderer()
    {
        m_resources.DestroyRenderTarget(m_shadowTarget);
    }

    void SceneRenderer::BuildLighting(Scene& scene, FrameLightingData& lighting)
    {
        lighting.Lights.clear();

        for (const auto [handle, component, transform] : scene.GetRegistry().view<LightComponent, TransformComponent>().each())
        {
            if (!transform.ActiveInHierarchy)
                continue;

            lighting.Lights.push_back(Light{
                .Type = component.Type,
                .Position = glm::vec3(transform.World[3]),
                .Direction = glm::normalize(glm::mat3(transform.World) * glm::vec3(0.0f, 0.0f, -1.0f)),
                .Color = component.Color,
                .Intensity = component.Intensity,
                .Range = component.Range,
                .InnerConeAngle = glm::radians(component.InnerConeDegrees),
                .OuterConeAngle = glm::radians(component.OuterConeDegrees),
                .CastsShadow = component.CastsShadow
            });
        }

        const uint32_t skipped = BuildShadowViews(lighting.Lights, m_atlas, lighting.Shadows);

        // This runs every frame, so only say it when it starts happening
        if (skipped > 0 && !m_atlasFullLogged)
            LOG_WARN("[Shadows] The atlas is full, {} lights will not cast shadows", skipped);

        m_atlasFullLogged = skipped > 0;
    }

    void SceneRenderer::RecordPasses(FrameContext& frame, Scene& scene, const FrameLightingData& lighting, const RenderPassInfo& mainPass)
    {
        m_drawables.clear();

        for (const auto [handle, renderer, transform] : scene.GetRegistry().view<MeshRendererComponent, TransformComponent>().each())
        {
            if (transform.ActiveInHierarchy && !renderer.Resolved.SubMeshes.empty())
                m_drawables.push_back({ &renderer.Resolved, frame.AddObject(transform.World), renderer.CastsShadows });
        }

        for (const ShadowView& view : lighting.Shadows)
        {
            RenderPassInfo info{
                .Target = m_shadowTarget,
                .ClearFlags = ClearFlags::Depth,
                .ViewProj = view.Shadow.ViewProj,
                .Viewport = view.Viewport
            };

            RenderPass& pass = frame.BeginRenderPass(info);

            for (const Drawable& drawable : m_drawables)
            {
                if (drawable.CastsShadows)
                    pass.Draw(*drawable.Mesh, drawable.Object, m_shadowMaterial);
            }
        }

        RenderPass& pass = frame.BeginRenderPass(mainPass);

        for (const Drawable& drawable : m_drawables)
            pass.Draw(*drawable.Mesh, drawable.Object);
    }
}
