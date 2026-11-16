#include "crimson/scene/scene_camera.hpp"

#include "crimson/scene/scene_serializer.hpp"

namespace crimson
{
    constexpr float kMinAxisLength = 1e-6f;
    constexpr float kMinCross = 1e-4f;

    // The direction an axis of the matrix points in, or nothing if it was squashed to a point
    static bool DirectionOf(const glm::mat4& world, const glm::vec3& axis, glm::vec3& direction)
    {
        const glm::vec3 scaled = glm::mat3(world) * axis;
        const float length = glm::length(scaled);
        direction = scaled / length;
        return length > kMinAxisLength;
    }

    static bool IsActiveInHierarchy(Entity entity)
    {
        for (entt::entity handle = entity.GetHandle(); handle != entt::null;
             handle = entity.GetScene()->GetRegistry().get<RelationshipComponent>(handle).Parent)
        {
            if (!entity.GetScene()->GetRegistry().get<NameComponent>(handle).Active)
                return false;
        }

        return true;
    }

    // Depth-first, in the order the Hierarchy shows them. Inactive subtrees are skipped.
    static Entity FirstActiveCamera(Scene& scene, entt::entity handle)
    {
        entt::registry& registry = scene.GetRegistry();

        if (!registry.get<NameComponent>(handle).Active)
            return {};

        if (registry.all_of<CameraComponent>(handle))
            return scene.GetEntity(handle);

        for (const entt::entity child : registry.get<RelationshipComponent>(handle).Children)
        {
            if (const Entity found = FirstActiveCamera(scene, child))
                return found;
        }

        return {};
    }

    CameraView ComputeCameraView(const CameraComponent& camera, const glm::mat4& world, float aspect)
    {
        const glm::vec3 position = glm::vec3(world[3]);
        glm::vec3 forward;
        glm::vec3 up;

        // Both go back to the defaults together, as one of each could point the same way and leave no view
        const bool valid = DirectionOf(world, glm::vec3(0.0f, 0.0f, -1.0f), forward) & DirectionOf(world, glm::vec3(0.0f, 1.0f, 0.0f), up);
        if (!valid || glm::length(glm::cross(forward, up)) < kMinCross)
        {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        CameraView view;
        view.Position = position;
        view.View = glm::lookAt(position, position + forward, up);

        if (camera.Projection == CameraProjection::Perspective)
        {
            view.Projection = glm::perspectiveRH_ZO(glm::radians(camera.FieldOfViewDegrees), aspect, camera.NearClip, camera.FarClip);
        }
        else
        {
            const float halfHeight = camera.OrthographicSize;
            const float halfWidth = halfHeight * aspect;
            view.Projection = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, camera.NearClip, camera.FarClip);
        }

        return view;
    }

    Entity FindMainCamera(Scene& scene, AssetID mainCamera)
    {
        // An unset id would match any entity that has no id yet
        const Entity referenced = mainCamera ? FindEntity(scene, mainCamera) : Entity{};
        if (referenced && referenced.HasComponent<CameraComponent>() && IsActiveInHierarchy(referenced))
            return referenced;

        for (const entt::entity root : scene.GetRoots())
        {
            if (const Entity found = FirstActiveCamera(scene, root))
                return found;
        }

        return {};
    }
}
