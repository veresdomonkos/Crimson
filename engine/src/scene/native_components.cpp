#include <algorithm>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/scene/component_registry.hpp"
#include "crimson/scene/scene_renderer.hpp"

namespace crimson
{
    namespace
    {
        const LightComponent& AsLight(const void* component) { return *static_cast<const LightComponent*>(component); }
    }

    void RegisterNativeComponents(ComponentRegistry& registry)
    {
        registry.RegisterNative<TransformComponent>("transform", "Transform")
            .InlineInFile()
            .Field<&TransformComponent::Position>({ .Key = "position", .Label = "Position", .Type = FieldType::Vec3, .Speed = 0.05f })
            .Field<&TransformComponent::RotationDegrees>({ .Key = "rotation", .Label = "Rotation", .Type = FieldType::Vec3, .Speed = 0.5f })
            .Field<&TransformComponent::Scale>({ .Key = "scale", .Label = "Scale", .Type = FieldType::Vec3, .Speed = 0.01f });

        // In LightType's order
        const std::vector<std::string> lightTypeKeys = { "directional", "point", "spot" };
        const std::vector<std::string> lightTypeLabels = { "Directional", "Point", "Spot" };

        const auto hasRange = [](const void* light) { return AsLight(light).Type != LightType::Directional; };
        const auto isSpot = [](const void* light) { return AsLight(light).Type == LightType::Spot; };

        registry.RegisterNative<LightComponent>("light", "Light")
            .Field<&LightComponent::Type>({ .Key = "type", .Label = "Type", .Type = FieldType::Enum, .EnumKeys = lightTypeKeys, .EnumLabels = lightTypeLabels })
            .Field<&LightComponent::Color>({ .Key = "color", .Label = "Color", .Type = FieldType::Color })
            .Field<&LightComponent::Intensity>({ .Key = "intensity", .Label = "Intensity", .Type = FieldType::Float, .Range = std::pair(0.0f, 1000.0f) })
            .Field<&LightComponent::Range>({ .Key = "range", .Label = "Range", .Type = FieldType::Float, .Range = std::pair(0.01f, 10000.0f), .Speed = 0.05f, .Visible = hasRange })
            .Field<&LightComponent::OuterConeDegrees>({ .Key = "outerCone", .Label = "Outer Cone", .Type = FieldType::Float, .Range = std::pair(1.0f, 89.0f), .Speed = 0.25f, .Visible = isSpot })
            .Field<&LightComponent::InnerConeDegrees>({ .Key = "innerCone", .Label = "Inner Cone", .Type = FieldType::Float, .Range = std::pair(0.0f, 89.0f), .Speed = 0.25f, .Visible = isSpot })
            .Field<&LightComponent::CastsShadow>({ .Key = "castsShadow", .Label = "Casts Shadow", .Type = FieldType::Bool })
            .Constrain([](LightComponent& light) { light.InnerConeDegrees = std::min(light.InnerConeDegrees, light.OuterConeDegrees); });

        registry.RegisterNative<MeshRendererComponent>("meshRenderer", "Mesh Renderer")
            .Field<&MeshRendererComponent::MeshAsset>({ .Key = "mesh", .Label = "Mesh", .Type = FieldType::Asset })
            .Field<&MeshRendererComponent::MaterialAssets>({ .Key = "materials", .Label = "Materials", .Type = FieldType::AssetList })
            .Field<&MeshRendererComponent::CastsShadows>({ .Key = "castsShadows", .Label = "Casts Shadows", .Type = FieldType::Bool })
            .OnLoaded([](Entity entity, AssetManager& loader) { ResolveMeshRenderer(entity.GetComponent<MeshRendererComponent>(), loader); })
            .OnCaptured([](std::vector<FieldValue>& values, const AssetManager& owner)
            {
                // The default is what an empty slot ends up with, and left empty it can still find the material made for it
                constexpr size_t kMaterials = 1;
                for (AssetID& material : std::get<std::vector<AssetID>>(values[kMaterials]))
                {
                    if (material == AssetID(owner.DefaultMaterial()))
                        material = AssetID{};
                }
            });
    }
}
