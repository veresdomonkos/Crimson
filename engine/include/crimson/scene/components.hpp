#pragma once
#include <string>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/renderer/light.hpp"
#include "crimson/renderer/mesh.hpp"

namespace crimson
{
    // Every entity has a name, a transform and a relationship, they can't be removed

    struct NameComponent
    {
        std::string Name;
        bool Active = true; // false also disables the children
        AssetID Id; // stays the same through saving and loading
    };

    struct TransformComponent
    {
        glm::vec3 Position{0.0f};
        glm::vec3 RotationDegrees{0.0f};
        glm::vec3 Scale{1.0f};

        // Set by Scene::UpdateWorldTransforms
        glm::mat4 World{1.0f};
        bool ActiveInHierarchy = true;

        [[nodiscard]] glm::mat4 GetLocalMatrix() const;

        // Overwrites position, rotation and scale from a matrix. Shear can't be stored, so it is lost.
        // The stored angles only change if the rotation really did, because converting a rotation
        // back to angles can give different values for the same rotation and make them jump.
        void SetFromMatrix(const glm::mat4& matrix);
    };

    struct RelationshipComponent
    {
        entt::entity Parent = entt::null;
        std::vector<entt::entity> Children;
    };

    // Position and direction come from the transform, a light points along its -Z axis
    struct LightComponent
    {
        LightType Type = LightType::Point;
        glm::vec3 Color{1.0f};
        float Intensity = 1.0f;
        float Range = 10.0f;
        float InnerConeDegrees = 20.0f;
        float OuterConeDegrees = 35.0f;
        bool CastsShadow = false;

        bool operator==(const LightComponent&) const = default;
    };

    // One component for what Unity splits into a mesh filter and a mesh renderer.
    // Call ResolveMeshRenderer after changing the assets.
    struct MeshRendererComponent
    {
        MeshID MeshAsset;
        std::vector<MaterialID> MaterialAssets; // one per material slot of the mesh
        bool CastsShadows = true;

        // The loaded assets, set by ResolveMeshRenderer
        Mesh Resolved;
    };

    // Everything below this entity comes from a scene's file, and is created when this is loaded
    struct SceneInstanceComponent
    {
        SceneID Scene;
    };

    // This entity came from an instance's scene, so it can't be changed here and isn't saved here
    struct InstancedComponent {};
}
