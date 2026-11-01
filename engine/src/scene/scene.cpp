#include "crimson/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "crimson/scene/entity.hpp"

namespace crimson
{
    glm::mat4 TransformComponent::GetLocalMatrix() const
    {
        const glm::quat rotation(glm::radians(RotationDegrees));
        return glm::translate(glm::mat4(1.0f), Position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), Scale);
    }

    void TransformComponent::SetFromMatrix(const glm::mat4& matrix)
    {
        glm::vec3 axisX(matrix[0]);
        const glm::vec3 axisY(matrix[1]);
        const glm::vec3 axisZ(matrix[2]);

        Position = glm::vec3(matrix[3]);
        Scale = glm::vec3(glm::length(axisX), glm::length(axisY), glm::length(axisZ));

        // A mirrored matrix has a negative determinant, which a rotation can't express
        if (glm::determinant(glm::mat3(matrix)) < 0.0f)
        {
            Scale.x = -Scale.x;
            axisX = -axisX;
        }

        constexpr float kMinScale = 0.000001f;
        if (std::abs(Scale.x) < kMinScale || std::abs(Scale.y) < kMinScale || std::abs(Scale.z) < kMinScale)
            return;

        const glm::mat3 rotationMatrix(glm::normalize(axisX), glm::normalize(axisY), glm::normalize(axisZ));
        const glm::quat rotation = glm::normalize(glm::quat_cast(rotationMatrix));
        const glm::quat current(glm::radians(RotationDegrees));

        constexpr float kSameRotation = 0.9999999f;
        if (std::abs(glm::dot(current, rotation)) < kSameRotation)
            RotationDegrees = glm::degrees(glm::eulerAngles(rotation));
    }

    Entity Scene::CreateEntity(std::string name, Entity parent)
    {
        const entt::entity handle = m_registry.create();

        m_registry.emplace<NameComponent>(handle, std::move(name));
        m_registry.emplace<TransformComponent>(handle);
        m_registry.emplace<RelationshipComponent>(handle);

        Attach(handle, parent ? parent.GetHandle() : entt::null);
        return { handle, this };
    }

    Entity Scene::CreateEntity(std::string name)
    {
        return CreateEntity(std::move(name), Entity{});
    }

    void Scene::DestroyEntity(Entity entity)
    {
        if (!entity)
            return;

        // Each child removes itself from this list, so it needs a copy
        const std::vector<entt::entity> children = entity.GetComponent<RelationshipComponent>().Children;
        for (const entt::entity child : children)
            DestroyEntity({ child, this });

        Detach(entity.GetHandle());
        m_registry.destroy(entity.GetHandle());
    }

    bool Scene::SetParent(Entity entity, Entity parent)
    {
        const entt::entity newParent = parent ? parent.GetHandle() : entt::null;

        for (entt::entity ancestor = newParent; ancestor != entt::null; ancestor = m_registry.get<RelationshipComponent>(ancestor).Parent)
        {
            if (ancestor == entity.GetHandle())
                return false;
        }

        if (entity.GetComponent<RelationshipComponent>().Parent == newParent)
            return true;

        const glm::mat4 world = ComputeWorldMatrix(entity);
        const glm::mat4 parentWorld = ComputeWorldMatrix(parent);

        Detach(entity.GetHandle());
        Attach(entity.GetHandle(), newParent);

        entity.GetComponent<TransformComponent>().SetFromMatrix(glm::inverse(parentWorld) * world);
        return true;
    }

    Entity Scene::GetEntity(entt::entity handle)
    {
        return { handle, this };
    }

    glm::mat4 Scene::ComputeWorldMatrix(Entity entity)
    {
        if (!entity)
            return glm::mat4(1.0f);

        const entt::entity parent = entity.GetComponent<RelationshipComponent>().Parent;
        const glm::mat4 local = entity.GetComponent<TransformComponent>().GetLocalMatrix();

        return parent == entt::null ? local : ComputeWorldMatrix({ parent, this }) * local;
    }

    // Returns how far along the ray it enters the box, or how far it leaves if it starts inside.
    // That way something in front of the camera wins over a box the camera is standing in.
    static std::optional<float> IntersectBounds(const glm::vec3& origin, const glm::vec3& direction, const MeshBounds& bounds)
    {
        float enter = 0.0f;
        float exit = std::numeric_limits<float>::max();
        bool startsInside = true;

        for (int axis = 0; axis < 3; ++axis)
        {
            if (origin[axis] < bounds.Min[axis] || origin[axis] > bounds.Max[axis])
                startsInside = false;

            if (direction[axis] == 0.0f)
            {
                if (origin[axis] < bounds.Min[axis] || origin[axis] > bounds.Max[axis])
                    return std::nullopt;

                continue;
            }

            const float toMin = (bounds.Min[axis] - origin[axis]) / direction[axis];
            const float toMax = (bounds.Max[axis] - origin[axis]) / direction[axis];

            enter = std::max(enter, std::min(toMin, toMax));
            exit = std::min(exit, std::max(toMin, toMax));

            if (enter > exit)
                return std::nullopt;
        }

        return startsInside ? exit : enter;
    }

    Entity Scene::Raycast(const glm::vec3& origin, const glm::vec3& direction)
    {
        entt::entity closest = entt::null;
        float closestDistance = std::numeric_limits<float>::max();

        for (const auto [handle, renderer, transform] : m_registry.view<MeshRendererComponent, TransformComponent>().each())
        {
            if (!transform.ActiveInHierarchy || renderer.Resolved.SubMeshes.empty())
                continue;

            // The direction isn't normalized afterwards, so distances stay comparable between entities
            const glm::mat4 toLocal = glm::inverse(transform.World);
            const glm::vec3 localOrigin(toLocal * glm::vec4(origin, 1.0f));
            const glm::vec3 localDirection(toLocal * glm::vec4(direction, 0.0f));

            const std::optional<float> distance = IntersectBounds(localOrigin, localDirection, renderer.Resolved.Bounds);

            if (distance && *distance < closestDistance)
            {
                closest = handle;
                closestDistance = *distance;
            }
        }

        return closest == entt::null ? Entity{} : Entity{ closest, this };
    }

    void Scene::UpdateWorldTransforms()
    {
        for (const entt::entity root : m_roots)
            UpdateWorldTransform(root, glm::mat4(1.0f), true);
    }

    void Scene::UpdateWorldTransform(entt::entity handle, const glm::mat4& parentWorld, bool parentActive)
    {
        TransformComponent& transform = m_registry.get<TransformComponent>(handle);

        transform.World = parentWorld * transform.GetLocalMatrix();
        transform.ActiveInHierarchy = parentActive && m_registry.get<NameComponent>(handle).Active;

        for (const entt::entity child : m_registry.get<RelationshipComponent>(handle).Children)
            UpdateWorldTransform(child, transform.World, transform.ActiveInHierarchy);
    }

    void Scene::Detach(entt::entity handle)
    {
        RelationshipComponent& relationship = m_registry.get<RelationshipComponent>(handle);

        std::vector<entt::entity>& siblings = relationship.Parent == entt::null
            ? m_roots
            : m_registry.get<RelationshipComponent>(relationship.Parent).Children;

        std::erase(siblings, handle);
        relationship.Parent = entt::null;
    }

    void Scene::Attach(entt::entity handle, entt::entity parent)
    {
        m_registry.get<RelationshipComponent>(handle).Parent = parent;

        if (parent == entt::null)
            m_roots.push_back(handle);
        else
            m_registry.get<RelationshipComponent>(parent).Children.push_back(handle);
    }
}
