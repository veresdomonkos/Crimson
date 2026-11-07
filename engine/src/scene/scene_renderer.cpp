#include "crimson/scene/scene_renderer.hpp"

#include <array>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    void ResolveMeshRenderer(MeshRendererComponent& renderer, AssetManager& assets)
    {
        renderer.Resolved = {};

        const Mesh* mesh = assets.LoadMesh(renderer.MeshAsset);
        if (!mesh)
            return;

        renderer.Resolved = *mesh;
        renderer.MaterialAssets.resize(renderer.Resolved.Materials.size());

        for (size_t slot = 0; slot < renderer.MaterialAssets.size(); ++slot)
        {
            // The default counts as empty, as that's how it's saved, so a slot material made since is found
            AssetID& asset = renderer.MaterialAssets[slot];
            if ((!asset || asset == assets.DefaultMaterial()) && slot < renderer.Resolved.MaterialSlots.size())
            {
                if (const AssetID found = assets.FindSlotMaterial(renderer.MeshAsset, renderer.Resolved.MaterialSlots[slot]))
                    asset = found;
            }

            if (!asset)
                asset = assets.DefaultMaterial();

            // A missing one stays in the slot, to be saved, and the default stands in for it
            renderer.Resolved.Materials[slot] = assets.LoadMaterial(assets.IsMissing(asset) ? assets.DefaultMaterial() : asset);
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

        if (m_hdrTarget)
            m_resources.DestroyRenderTarget(m_hdrTarget);

        if (m_outlineMaskTarget)
            m_resources.DestroyRenderTarget(m_outlineMaskTarget);

        if (m_screenTriangleVertices)
        {
            m_resources.DestroyVertexBuffer(m_screenTriangleVertices);
            m_resources.DestroyIndexBuffer(m_screenTriangleIndices);
        }
    }

    void SceneRenderer::CreateScreenTriangle()
    {
        if (m_screenTriangleVertices)
            return;

        // A triangle that covers the whole screen, the parts outside of it get clipped
        constexpr std::array<glm::vec3, 3> vertices = { glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(3.0f, -1.0f, 0.0f), glm::vec3(-1.0f, 3.0f, 0.0f) };
        constexpr std::array<uint32_t, 3> indices = { 0, 1, 2 };

        m_screenTriangleVertices = m_resources.CreateVertexBuffer({ .Layout = { ShaderDataType::Float3 }, .Size = sizeof(vertices), .Usage = BufferUsage::Static }, vertices.data());
        m_screenTriangleIndices = m_resources.CreateIndexBuffer({ .Size = sizeof(indices), .Usage = BufferUsage::Static, .Type = IndexType::UInt32 }, indices.data());
    }

    void SceneRenderer::SetToneMapMaterial(MaterialHandle material, uint32_t width, uint32_t height)
    {
        if (!material)
            return;

        m_toneMapMaterial = material;

        const RenderTargetInfo info{ .Width = width, .Height = height, .ColorFormats = { TextureFormat::RGBA16F }, .DepthFormat = TextureFormat::Depth32F };
        m_hdrTarget = m_resources.CreateRenderTarget(info);
        m_resources.SetMaterialTexture(material, "SceneColor", m_resources.GetColorAttachment(m_hdrTarget, 0));

        CreateScreenTriangle();
    }

    void SceneRenderer::SetToneMapping(float exposure, ToneMapper mapper)
    {
        if (!m_toneMapMaterial)
            return;

        m_resources.SetMaterialPropertyByName(m_toneMapMaterial, "Exposure", exposure);
        m_resources.SetMaterialPropertyByName(m_toneMapMaterial, "ToneMapper", static_cast<float>(mapper));
    }

    void SceneRenderer::SetOutlineMaterials(MaterialHandle mask, MaterialHandle outline, uint32_t width, uint32_t height)
    {
        if (!mask || !outline)
            return;

        m_outlineMaskMaterial = mask;
        m_outlineMaterial = outline;

        const RenderTargetInfo info{ .Width = width, .Height = height, .ColorFormats = { TextureFormat::RGBA8 } };
        m_outlineMaskTarget = m_resources.CreateRenderTarget(info);
        m_resources.SetMaterialTexture(outline, "OutlineMask", m_resources.GetColorAttachment(m_outlineMaskTarget, 0));

        CreateScreenTriangle();
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

    void SceneRenderer::RecordPasses(FrameContext& frame, Scene& scene, const FrameLightingData& lighting, const RenderPassInfo& mainPass, Entity outlined)
    {
        m_drawables.clear();

        const bool outlining = outlined && m_outlineMaskTarget;
        bool anyOutlined = false;

        for (const auto [handle, renderer, transform] : scene.GetRegistry().view<MeshRendererComponent, TransformComponent>().each())
        {
            if (!transform.ActiveInHierarchy || renderer.Resolved.SubMeshes.empty())
                continue;

            bool isOutlined = false;

            if (outlining)
            {
                for (entt::entity ancestor = handle; ancestor != entt::null && !isOutlined; ancestor = scene.GetRegistry().get<RelationshipComponent>(ancestor).Parent)
                    isOutlined = ancestor == outlined.GetHandle();
            }

            anyOutlined = anyOutlined || isOutlined;
            m_drawables.push_back({ &renderer.Resolved, frame.AddObject(transform.World), renderer.CastsShadows, isOutlined });
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

        if (anyOutlined)
            RecordOutlineMask(frame, mainPass);

        RenderPassInfo sceneInfo = mainPass;
        if (m_hdrTarget)
            sceneInfo.Target = m_hdrTarget;

        RenderPass& pass = frame.BeginRenderPass(sceneInfo);

        for (const Drawable& drawable : m_drawables)
            pass.Draw(*drawable.Mesh, drawable.Object);

        if (m_hdrTarget)
        {
            // Depth too, for a target that has it, as this draws in front of whatever is left in there
            const RenderPassInfo toneMapInfo{ .Target = mainPass.Target, .ClearFlags = ClearFlags::Color | ClearFlags::Depth, .Viewport = mainPass.Viewport };
            frame.BeginRenderPass(toneMapInfo).Draw(DrawInfo{ m_screenTriangleVertices, m_screenTriangleIndices, m_toneMapMaterial });
        }

        // In display colors, after the tone mapping, so that it stays the color it was set to
        if (anyOutlined)
        {
            const RenderPassInfo outlineInfo{ .Target = mainPass.Target, .Viewport = mainPass.Viewport };
            frame.BeginRenderPass(outlineInfo).Draw(DrawInfo{ m_screenTriangleVertices, m_screenTriangleIndices, m_outlineMaterial });
        }
    }

    void SceneRenderer::RecordOutlineMask(FrameContext& frame, const RenderPassInfo& mainPass)
    {
        RenderPassInfo info{
            .Target = m_outlineMaskTarget,
            .ClearFlags = ClearFlags::Color,
            .ClearColor = glm::vec4(0.0f),
            .ViewProj = mainPass.ViewProj,
            .CameraPosition = mainPass.CameraPosition
        };

        RenderPass& pass = frame.BeginRenderPass(info);

        for (const Drawable& drawable : m_drawables)
        {
            if (drawable.Outlined)
                pass.Draw(*drawable.Mesh, drawable.Object, m_outlineMaskMaterial);
        }
    }
}
