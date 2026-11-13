#include <algorithm>
#include <string>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/scene/component_builder.hpp"

namespace crimson
{
    namespace
    {
        // Loads the renderer's mesh and materials into Resolved and sizes MaterialAssets to the mesh's slots.
        // Empty slots get the material made for them (AssetManager::FindSlotMaterial), or else the default one.
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

        std::string SlotName(const MeshRendererComponent& renderer, size_t slot)
        {
            return slot < renderer.Resolved.MaterialSlots.size() ? renderer.Resolved.MaterialSlots[slot] : "Material " + std::to_string(slot);
        }

        // The default is what an empty slot ends up with
        AssetID DefaultMaterialOf(const AssetManager& assets)
        {
            return assets.DefaultMaterial();
        }

        // The engine's built-in meshes, in engine/resources/meshes
        struct BuiltInMesh
        {
            const char* Name;
            const char* Path;
        };

        constexpr BuiltInMesh kBuiltInMeshes[] = {
            { "Cube",     "engine/meshes/SM_Cube.obj" },
            { "Sphere",   "engine/meshes/SM_Sphere.obj" },
            { "Cylinder", "engine/meshes/SM_Cylinder.obj" },
            { "Capsule",  "engine/meshes/SM_Capsule.obj" },
            { "Plane",    "engine/meshes/SM_Plane.obj" },
            { "Quad",     "engine/meshes/SM_Quad.obj" },
        };
    }

    void RegisterNativeComponents(ComponentRegistry& registry)
    {
        registry.Register<TransformComponent>("transform", "Transform")
            .PartOfNode()
            .Icon("move-3d")
            .Field("position", "Position", &TransformComponent::Position, { .Speed = 0.05f })
            .Field("rotation", "Rotation", &TransformComponent::RotationDegrees, { .Speed = 0.5f })
            .Field("scale", "Scale", &TransformComponent::Scale, { .UniformToggle = true });

        const auto hasRange = [](const LightComponent& light) { return light.Type != LightType::Directional; };
        const auto isSpot = [](const LightComponent& light) { return light.Type == LightType::Spot; };

        registry.Register<LightComponent>("light", "Light")
            .Icon([](const LightComponent& light) -> std::string_view
            {
                switch (light.Type)
                {
                    case LightType::Directional: return "sun";
                    case LightType::Point:       return "lightbulb";
                    case LightType::Spot:        return "cone";
                }

                return {};
            })
            .Enum("type", "Type", &LightComponent::Type, { { "directional", "Directional" }, { "point", "Point" }, { "spot", "Spot" } })
            .Color("color", "Color", &LightComponent::Color)
            .Field("intensity", "Intensity", &LightComponent::Intensity, { .Range = std::pair(0.0f, 1000.0f) })
            .Field("range", "Range", &LightComponent::Range, { .Range = std::pair(0.01f, 10000.0f), .Speed = 0.05f, .VisibleIf = hasRange })
            .Field("outerCone", "Outer Cone", &LightComponent::OuterConeDegrees, { .Range = std::pair(1.0f, 89.0f), .Speed = 0.25f, .VisibleIf = isSpot })
            .Field("innerCone", "Inner Cone", &LightComponent::InnerConeDegrees, { .Range = std::pair(0.0f, 89.0f), .Speed = 0.25f, .VisibleIf = isSpot })
            .Field("castsShadow", "Casts Shadow", &LightComponent::CastsShadow)
            .Constrain([](LightComponent& light) { light.InnerConeDegrees = std::min(light.InnerConeDegrees, light.OuterConeDegrees); })
            .Preset("Light", "Directional", [](LightComponent& light, const AssetManager&) { light.Type = LightType::Directional; })
            .Preset("Light", "Point", [](LightComponent& light, const AssetManager&) { light.Type = LightType::Point; })
            .Preset("Light", "Spot", [](LightComponent& light, const AssetManager&) { light.Type = LightType::Spot; });

        auto meshRenderer = registry.Register<MeshRendererComponent>("meshRenderer", "Mesh Renderer");
        meshRenderer.Icon("box")
            .Asset("mesh", "Mesh", &MeshRendererComponent::MeshAsset, { .SpawnsFrom = true })
            .AssetList("materials", "Materials", &MeshRendererComponent::MaterialAssets, { .ItemLabel = SlotName, .Fallback = DefaultMaterialOf, .DropTarget = true })
            .Field("castsShadows", "Casts Shadows", &MeshRendererComponent::CastsShadows)
            .ResolveAssets(ResolveMeshRenderer);

        // The default material fills the mesh's one slot when it is resolved
        for (const BuiltInMesh& mesh : kBuiltInMeshes)
        {
            meshRenderer.Preset("3D Object", mesh.Name, [path = mesh.Path](MeshRendererComponent& renderer, const AssetManager& assets)
            {
                renderer.MeshAsset = assets.Find<AssetType::Mesh>(path);
            });
        }
    }
}
