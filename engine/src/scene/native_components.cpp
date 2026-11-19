#include <algorithm>
#include <string>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/asset_manager/slot_materials.hpp"
#include "crimson/scene/component_builder.hpp"

namespace crimson
{
    namespace
    {
        // The smallest distance the far clip keeps beyond the near one
        constexpr float kMinClipGap = 0.01f;

        // One item per slot of the renderer's mesh, named after the slot, whose empty entry uses the material made for the
        // slot or else the default one. Without a mesh, one per entry it holds.
        std::vector<AssetListItem> MaterialSlots(const MeshRendererComponent& renderer, AssetManager& assets)
        {
            std::vector<AssetListItem> items;

            if (const Mesh* mesh = assets.Get(renderer.MeshAsset))
            {
                for (const std::string& slot : mesh->MaterialSlots)
                    items.push_back({ slot, slot_materials::ForEmptySlot(assets, renderer.MeshAsset, slot) });
            }
            else
            {
                for (size_t index = 0; index < renderer.MaterialAssets.size(); ++index)
                    items.push_back({ "Material " + std::to_string(index), slot_materials::DefaultMaterial(assets) });
            }

            return items;
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

        const auto isPerspective = [](const CameraComponent& camera) { return camera.Projection == CameraProjection::Perspective; };
        const auto isOrthographic = [](const CameraComponent& camera) { return camera.Projection == CameraProjection::Orthographic; };

        registry.Register<CameraComponent>("camera", "Camera")
            .Icon("video")
            .Enum("projection", "Projection", &CameraComponent::Projection, { { "perspective", "Perspective" }, { "orthographic", "Orthographic" } })
            .Field("fov", "Field of View", &CameraComponent::FieldOfViewDegrees, { .Range = std::pair(1.0f, 179.0f), .Speed = 0.25f, .VisibleIf = isPerspective })
            .Field("orthographicSize", "Size", &CameraComponent::OrthographicSize, { .Range = std::pair(0.001f, 100000.0f), .Speed = 0.05f, .VisibleIf = isOrthographic })
            .Field("near", "Near Clip", &CameraComponent::NearClip, { .Range = std::pair(0.001f, 10000.0f), .Speed = 0.01f })
            .Field("far", "Far Clip", &CameraComponent::FarClip, { .Range = std::pair(0.01f, 100000.0f), .Speed = 1.0f })
            .Constrain([](CameraComponent& camera) { camera.FarClip = std::max(camera.FarClip, camera.NearClip + kMinClipGap); })
            .Preset("Camera", "Camera", [](CameraComponent&, const AssetManager&) {});

        auto meshRenderer = registry.Register<MeshRendererComponent>("meshRenderer", "Mesh Renderer");
        meshRenderer.Icon("box")
            .Asset("mesh", "Mesh", &MeshRendererComponent::MeshAsset, { .SpawnsFrom = true })
            .AssetList("materials", "Materials", &MeshRendererComponent::MaterialAssets, { .Items = MaterialSlots, .DropTarget = true })
            .Field("castsShadows", "Casts Shadows", &MeshRendererComponent::CastsShadows);

        for (const BuiltInMesh& mesh : kBuiltInMeshes)
        {
            meshRenderer.Preset("3D Object", mesh.Name, [path = mesh.Path](MeshRendererComponent& renderer, const AssetManager& assets)
            {
                renderer.MeshAsset = assets.Find<MeshAsset>(path);
            });
        }
    }
}
