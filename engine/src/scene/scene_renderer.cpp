#include "crimson/scene/scene_renderer.hpp"

#include <algorithm>
#include <array>
#include <cassert>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/asset_manager/slot_materials.hpp"
#include "crimson/core/log.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    SceneRenderer::SceneRenderer(GpuResourceManager& resources, Renderer& renderer, AssetManager& assets, ShadowAtlasSettings atlas)
        : m_resources(resources), m_assets(assets), m_atlas(atlas)
    {
        const RenderTargetInfo info{ .Width = atlas.AtlasSize, .Height = atlas.AtlasSize, .DepthFormat = TextureFormat::Depth32F };
        m_shadowTarget = resources.CreateRenderTarget(info);
        m_shadowAtlas = resources.GetDepthAttachment(m_shadowTarget).value();

        renderer.SetShadowAtlas(m_shadowAtlas);
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

    // The material an id gives now. If that is another handle than the one it was set up on, it was made again, and
    // setUp does what it needs. That runs inside a frame, which is safe as a handle that changed was made between frames,
    // so none in flight uses it.
    template <typename SetUp>
    static MaterialHandle FollowMaterial(AssetManager& assets, MaterialID id, MaterialHandle& setUp, SetUp&& setUpNew)
    {
        const MaterialHandle* found = assets.Get(id);
        if (!found)
            return {};

        if (*found != setUp)
        {
            setUp = *found;
            setUpNew(*found);
        }

        return *found;
    }

    void SceneRenderer::SetUpToneMap(MaterialHandle material)
    {
        m_resources.SetMaterialTexture(material, "SceneColor", m_hdrColor);
        ApplyToneMapping(material);
    }

    void SceneRenderer::ApplyToneMapping(MaterialHandle material)
    {
        if (!m_toneMapping)
            return;

        m_resources.SetMaterialPropertyByName(material, "Exposure", m_toneMapping->first);
        m_resources.SetMaterialPropertyByName(material, "ToneMapper", static_cast<float>(m_toneMapping->second));
    }

    void SceneRenderer::SetToneMapMaterial(MaterialID material, uint32_t width, uint32_t height)
    {
        m_toneMapMaterial = material;

        const MaterialHandle* handle = m_assets.Get(material);
        if (!handle)
            return;

        const RenderTargetInfo info{ .Width = width, .Height = height, .ColorFormats = { TextureFormat::RGBA16F }, .DepthFormat = TextureFormat::Depth32F };
        m_hdrTarget = m_resources.CreateRenderTarget(info);
        m_hdrColor = m_resources.GetColorAttachment(m_hdrTarget, 0);

        m_toneMapSetUp = *handle;
        SetUpToneMap(*handle);

        CreateScreenTriangle();
    }

    void SceneRenderer::SetToneMapping(float exposure, ToneMapper mapper)
    {
        m_toneMapping = std::make_pair(exposure, mapper);

        if (m_hdrTarget)
        {
            // On the handle it has now, which a new one would have been set up on
            const MaterialHandle handle = FollowMaterial(m_assets, m_toneMapMaterial, m_toneMapSetUp, [this](MaterialHandle made) { SetUpToneMap(made); });
            if (handle)
                ApplyToneMapping(handle);
        }
    }

    void SceneRenderer::SetOutlineMaterial(MaterialID outline, uint32_t width, uint32_t height)
    {
        m_outlineMaterial = outline;

        const MaterialHandle* handle = m_assets.Get(outline);
        if (!handle)
            return;

        // The outlined meshes' depth, drawn like a shadow, so their shaders decide their shape there too
        const RenderTargetInfo info{ .Width = width, .Height = height, .DepthFormat = TextureFormat::Depth32F };
        m_outlineMaskTarget = m_resources.CreateRenderTarget(info);
        m_outlineMask = m_resources.GetDepthAttachment(m_outlineMaskTarget).value();

        m_outlineSetUp = *handle;
        m_resources.SetMaterialTexture(*handle, "OutlineMask", m_outlineMask);

        CreateScreenTriangle();
    }

    void SceneRenderer::BuildLighting(Scene& scene, FrameLighting& lighting)
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
            LOG_WARN("[Shadows] No room for more shadows, {} lights will not cast them", skipped);

        m_atlasFullLogged = skipped > 0;
    }

    const std::vector<MaterialID>& SceneRenderer::EmptySlotMaterials(MeshID mesh, const Mesh& loaded)
    {
        auto [it, added] = m_emptySlotMaterials.try_emplace(mesh);

        if (added)
        {
            for (const std::string& slot : loaded.MaterialSlots)
                it->second.push_back(slot_materials::ForEmptySlot(m_assets, mesh, slot));
        }

        return it->second;
    }

    void SceneRenderer::Record(FrameRecorder& frame, Scene& scene, const PassInfo& mainPass, Entity outlined)
    {
        m_drawables.clear();
        m_materials.clear();

        // What the empty slots use depends on which assets exist, so it is worked out again when those change
        if (m_cachedVersion != m_assets.Version())
        {
            m_emptySlotMaterials.clear();
            m_missingMaterial = slot_materials::MissingMaterial(m_assets);
            m_cachedVersion = m_assets.Version();
        }

        const bool outlining = outlined && m_outlineMaskTarget;
        bool anyOutlined = false;

        for (const auto [handle, renderer, transform] : scene.GetRegistry().view<MeshRendererComponent, TransformComponent>().each())
        {
            if (!transform.ActiveInHierarchy)
                continue;

            // Looked up every frame, so a mesh or material that was made again is the one drawn
            const Mesh* mesh = m_assets.Get(renderer.MeshAsset);
            if (!mesh || mesh->SubMeshes.empty())
                continue;

            bool isOutlined = false;

            if (outlining)
            {
                for (entt::entity ancestor = handle; ancestor != entt::null && !isOutlined; ancestor = scene.GetRegistry().get<RelationshipComponent>(ancestor).Parent)
                    isOutlined = ancestor == outlined.GetHandle();
            }

            anyOutlined = anyOutlined || isOutlined;

            const auto first = static_cast<uint32_t>(m_materials.size());
            const std::vector<MaterialID>& empty = EmptySlotMaterials(renderer.MeshAsset, *mesh);

            for (size_t slot = 0; slot < mesh->MaterialSlots.size(); ++slot)
            {
                const MaterialID chosen = slot < renderer.MaterialAssets.size() ? renderer.MaterialAssets[slot] : MaterialID{};
                const MaterialID id = chosen ? chosen : empty[slot];

                // One that isn't there or doesn't load is drawn as the missing material, to be seen, so no pass leaves it out
                const MaterialHandle* material = m_assets.Get(id);
                if (!material)
                    material = m_assets.Get(m_missingMaterial);

                m_materials.push_back(material ? *material : MaterialHandle::Invalid());
            }

            m_drawables.push_back({ mesh, first, static_cast<uint32_t>(m_materials.size()) - first, frame.AddObject(transform.World), renderer.CastsShadows, isOutlined });
        }

        RecordShadows(frame);

        if (anyOutlined)
            RecordOutlineMask(frame, mainPass);

        PassInfo sceneInfo = mainPass;
        sceneInfo.Name = "Scene";

        // Next to what the caller's pass samples
        const auto freeRead = std::find(sceneInfo.Reads.begin(), sceneInfo.Reads.end(), TextureHandle{});
        assert(freeRead != sceneInfo.Reads.end() && "The main pass reads too many textures to add the shadow atlas");
        if (freeRead != sceneInfo.Reads.end())
            *freeRead = m_shadowAtlas;

        if (m_hdrTarget)
            sceneInfo.Target = m_hdrTarget;

        PassBuilder pass = frame.AddPass(sceneInfo);

        for (const Drawable& drawable : m_drawables)
            pass.DrawMesh(*drawable.Mesh, MaterialsOf(drawable), drawable.Object);

        if (m_hdrTarget)
        {
            // Depth too, for a target that has it, as this draws in front of whatever is left in there
            const PassInfo toneMapInfo{
                .Name = "Tone Mapping",
                .Target = mainPass.Target,
                .Clear = { .Flags = ClearFlags::Color | ClearFlags::Depth },
                .Viewport = mainPass.Viewport,
                .Reads = { m_hdrColor }
            };
            const MaterialHandle toneMap = FollowMaterial(m_assets, m_toneMapMaterial, m_toneMapSetUp, [this](MaterialHandle made) { SetUpToneMap(made); });
            frame.AddPass(toneMapInfo).Draw(DrawItem{ m_screenTriangleVertices, m_screenTriangleIndices, toneMap });
        }

        // In display colors, after the tone mapping, so that it stays the color it was set to
        if (anyOutlined)
        {
            const PassInfo outlineInfo{ .Name = "Outline", .Target = mainPass.Target, .Viewport = mainPass.Viewport, .Reads = { m_outlineMask } };
            const MaterialHandle outline = FollowMaterial(m_assets, m_outlineMaterial, m_outlineSetUp, [this](MaterialHandle made) { m_resources.SetMaterialTexture(made, "OutlineMask", m_outlineMask); });
            frame.AddPass(outlineInfo).Draw(DrawItem{ m_screenTriangleVertices, m_screenTriangleIndices, outline });
        }
    }

    void SceneRenderer::RecordShadows(FrameRecorder& frame)
    {
        if (frame.Lighting().Shadows.empty())
            return;

        // One pass for the whole atlas, so the number of shadows isn't limited by the frame's passes.
        // It clears every tile, and each view then draws into its own.
        PassBuilder pass = frame.AddPass({ .Name = "Shadows", .Target = m_shadowTarget, .Clear = { .Flags = ClearFlags::Depth } });

        for (const ShadowView& shadow : frame.Lighting().Shadows)
        {
            const ViewId view = frame.AddView({ .ViewProj = shadow.Shadow.ViewProj, .CameraPosition = shadow.Eye }, shadow.Viewport);

            for (const Drawable& drawable : m_drawables)
            {
                if (drawable.CastsShadows)
                    pass.DrawMesh(*drawable.Mesh, MaterialsOf(drawable), drawable.Object, { .Variant = ShaderVariant::Depth, .View = view });
            }
        }
    }

    void SceneRenderer::RecordOutlineMask(FrameRecorder& frame, const PassInfo& mainPass)
    {
        const PassInfo info{
            .Name = "Outline Mask",
            .Target = m_outlineMaskTarget,
            .Clear = { .Flags = ClearFlags::Depth },
            .View = mainPass.View
        };

        PassBuilder pass = frame.AddPass(info);

        for (const Drawable& drawable : m_drawables)
        {
            if (drawable.Outlined)
                pass.DrawMesh(*drawable.Mesh, MaterialsOf(drawable), drawable.Object, { .Variant = ShaderVariant::Depth });
        }
    }
}
