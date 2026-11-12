#include "crimson/scene/scene_renderer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <type_traits>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"
#include "crimson/renderer/bounds.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    namespace
    {
        // FNV-1a. A collision would keep a tile that changed, which 64 bits make unlikely enough.
        constexpr uint64_t kHashSeed = 14695981039346656037ull;

        // Only for types without padding, which would make equal values hash differently
        template <typename T>
        void Hash(uint64_t& hash, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);

            std::array<unsigned char, sizeof(T)> bytes;
            std::memcpy(bytes.data(), &value, sizeof(T));

            for (const unsigned char byte : bytes)
            {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
        }
    }

    void ResolveMeshRenderer(MeshRendererComponent& renderer, AssetManager& assets)
    {
        renderer.Resolved = {};

        const Mesh* mesh = assets.Load(renderer.MeshAsset);
        if (!mesh)
            return;

        renderer.Resolved = *mesh;
        renderer.MaterialAssets.resize(renderer.Resolved.Materials.size());

        for (size_t slot = 0; slot < renderer.MaterialAssets.size(); ++slot)
        {
            // The default counts as empty, as that's how it's saved, so a slot material made since is found
            MaterialID& asset = renderer.MaterialAssets[slot];
            if ((!asset || asset == assets.DefaultMaterial()) && slot < renderer.Resolved.MaterialSlots.size())
            {
                if (const MaterialID found = assets.FindSlotMaterial(renderer.MeshAsset, renderer.Resolved.MaterialSlots[slot]))
                    asset = found;
            }

            if (!asset)
                asset = assets.DefaultMaterial();

            // A missing one stays in the slot, to be saved, and the default stands in for it
            renderer.Resolved.Materials[slot] = assets.Load(assets.IsMissing(asset) ? assets.DefaultMaterial() : asset);
        }
    }

    SceneRenderer::SceneRenderer(GpuResourceManager& resources, Renderer& renderer, ShadowAtlasSettings atlas)
        : m_resources(resources), m_renderer(renderer), m_atlas(atlas)
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

    void SceneRenderer::SetToneMapMaterial(MaterialHandle material, uint32_t width, uint32_t height)
    {
        if (!material)
            return;

        m_toneMapMaterial = material;

        const RenderTargetInfo info{ .Width = width, .Height = height, .ColorFormats = { TextureFormat::RGBA16F }, .DepthFormat = TextureFormat::Depth32F };
        m_hdrTarget = m_resources.CreateRenderTarget(info);
        m_hdrColor = m_resources.GetColorAttachment(m_hdrTarget, 0);
        m_resources.SetMaterialTexture(material, "SceneColor", m_hdrColor);

        CreateScreenTriangle();
    }

    void SceneRenderer::SetToneMapping(float exposure, ToneMapper mapper)
    {
        if (!m_toneMapMaterial)
            return;

        m_resources.SetMaterialPropertyByName(m_toneMapMaterial, "Exposure", exposure);
        m_resources.SetMaterialPropertyByName(m_toneMapMaterial, "ToneMapper", static_cast<float>(mapper));
    }

    void SceneRenderer::SetOutlineMaterial(MaterialHandle outline, uint32_t width, uint32_t height)
    {
        if (!outline)
            return;

        m_outlineMaterial = outline;

        // The outlined meshes' depth, drawn like a shadow, so their shaders decide their shape there too
        const RenderTargetInfo info{ .Width = width, .Height = height, .DepthFormat = TextureFormat::Depth32F };
        m_outlineMaskTarget = m_resources.CreateRenderTarget(info);
        m_outlineMask = m_resources.GetDepthAttachment(m_outlineMaskTarget).value();
        m_resources.SetMaterialTexture(outline, "OutlineMask", m_outlineMask);

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

    void SceneRenderer::Record(FrameRecorder& frame, Scene& scene, const PassInfo& mainPass, Entity outlined)
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

            Drawable& drawable = m_drawables.emplace_back(Drawable{ &renderer.Resolved, frame.AddObject(transform.World), renderer.CastsShadows, isOutlined });
            if (drawable.CastsShadows)
                AddShadowCaster(drawable, transform.World);
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
            pass.DrawMesh(*drawable.Mesh, drawable.Object);

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
            frame.AddPass(toneMapInfo).Draw(DrawItem{ m_screenTriangleVertices, m_screenTriangleIndices, m_toneMapMaterial });
        }

        // In display colors, after the tone mapping, so that it stays the color it was set to
        if (anyOutlined)
        {
            const PassInfo outlineInfo{ .Name = "Outline", .Target = mainPass.Target, .Viewport = mainPass.Viewport, .Reads = { m_outlineMask } };
            frame.AddPass(outlineInfo).Draw(DrawItem{ m_screenTriangleVertices, m_screenTriangleIndices, m_outlineMaterial });
        }
    }

    void SceneRenderer::AddShadowCaster(Drawable& drawable, const glm::mat4& world)
    {
        const Mesh& mesh = *drawable.Mesh;

        drawable.WorldBounds = TransformBounds(mesh.Bounds, world);

        // Everything that the depth pass draws it with, so a moved, swapped or edited mesh changes it
        uint64_t signature = kHashSeed;
        Hash(signature, world);
        Hash(signature, mesh.VertexBuffer.GetRaw());
        Hash(signature, mesh.IndexBuffer.GetRaw());

        for (const SubMesh& subMesh : mesh.SubMeshes)
        {
            Hash(signature, subMesh.FirstIndex);
            Hash(signature, subMesh.IndexCount);

            const MaterialHandle material = mesh.Materials[subMesh.MaterialSlot];
            Hash(signature, material.GetRaw());
            drawable.ShadowChangesOverTime = drawable.ShadowChangesOverTime || DepthReadsTime(material);
        }

        drawable.ShadowSignature = signature;
    }

    bool SceneRenderer::DepthReadsTime(MaterialHandle material)
    {
        // A changed material may have a new shader
        if (m_depthReadsTimeRevision != m_resources.GetMaterialRevision())
        {
            m_depthReadsTime.clear();
            m_depthReadsTimeRevision = m_resources.GetMaterialRevision();
        }

        const auto [found, added] = m_depthReadsTime.try_emplace(material.GetRaw(), false);
        if (added)
        {
            const ShaderHandle shader = m_resources.GetMaterialShader(material);
            found->second = shader && m_resources.GetShaderInterface(shader).DepthReadsTime;
        }

        return found->second;
    }

    void SceneRenderer::RecordShadows(FrameRecorder& frame)
    {
        // A dropped frame's tiles never reached the atlas
        if (m_renderer.GetSubmittedFrames() != m_expectedFrames)
            m_shadowTiles.clear();

        m_expectedFrames = m_renderer.GetSubmittedFrames() + 1;
        m_shadowStats = {};

        const std::vector<ShadowView>& shadows = frame.Lighting().Shadows;

        // A tile that comes back into use is drawn again, whatever was left in it
        m_shadowTiles.resize(shadows.size());

        // One pass for the whole atlas, so the number of shadows isn't limited by the frame's passes. It is only added
        // for a tile that changed, which it clears, and the others keep what the frames before drew.
        std::optional<PassBuilder> pass;

        for (size_t index = 0; index < shadows.size(); ++index)
        {
            const ShadowView& shadow = shadows[index];

            uint64_t signature = kHashSeed;
            Hash(signature, shadow.Shadow.ViewProj);
            Hash(signature, shadow.Viewport);
            Hash(signature, m_resources.GetMaterialRevision());

            bool changesOverTime = false;

            // A caster that left the view changes it as much as one that came in
            for (const Drawable& drawable : m_drawables)
            {
                if (!drawable.CastsShadows || IsOutsideView(drawable.WorldBounds, shadow.Shadow.ViewProj))
                    continue;

                Hash(signature, drawable.ShadowSignature);
                changesOverTime = changesOverTime || drawable.ShadowChangesOverTime;
            }

            std::optional<uint64_t>& tile = m_shadowTiles[index];

            if (tile == signature)
            {
                ++m_shadowStats.Kept;
                continue;
            }

            if (!pass)
                pass = frame.AddPass({ .Name = "Shadows", .Target = m_shadowTarget });

            pass->ClearArea(shadow.Viewport, { .Flags = ClearFlags::Depth });

            const ViewId view = frame.AddView({ .ViewProj = shadow.Shadow.ViewProj, .CameraPosition = shadow.Eye }, shadow.Viewport);

            for (const Drawable& drawable : m_drawables)
            {
                if (drawable.CastsShadows)
                    pass->DrawMesh(*drawable.Mesh, drawable.Object, { .Variant = ShaderVariant::Depth, .View = view });
            }

            // One that moves with the time is drawn again next frame too
            tile = changesOverTime ? std::nullopt : std::optional(signature);
            ++m_shadowStats.Drawn;
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
                pass.DrawMesh(*drawable.Mesh, drawable.Object, { .Variant = ShaderVariant::Depth });
        }
    }
}
