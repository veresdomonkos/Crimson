#pragma once
#include <span>
#include <string>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

namespace crimson
{
    class Entity;

    class Scene
    {
    public:
        Entity CreateEntity(std::string name, Entity parent);
        Entity CreateEntity(std::string name);

        // Also destroys the children
        void DestroyEntity(Entity entity);

        // Destroys every entity
        void Clear();

        // An invalid parent makes the entity a root. It stays where it is in the world.
        // Returns false without changing anything if the parent is the entity or one of its children.
        bool SetParent(Entity entity, Entity parent);

        [[nodiscard]] Entity GetEntity(entt::entity handle);
        [[nodiscard]] std::span<const entt::entity> GetRoots() const { return m_roots; }

        // Always current, unlike TransformComponent::World. An invalid entity gives the identity.
        [[nodiscard]] glm::mat4 ComputeWorldMatrix(Entity entity);

        // The nearest active mesh renderer whose bounds the ray hits, or an invalid entity.
        // Uses the world transforms of the last UpdateWorldTransforms.
        [[nodiscard]] Entity Raycast(const glm::vec3& origin, const glm::vec3& direction);

        // Call once a frame before reading TransformComponent::World or ActiveInHierarchy
        void UpdateWorldTransforms();

        [[nodiscard]] entt::registry& GetRegistry() { return m_registry; }
    private:
        void UpdateWorldTransform(entt::entity handle, const glm::mat4& parentWorld, bool parentActive);
        void Detach(entt::entity handle);
        void Attach(entt::entity handle, entt::entity parent);
    private:
        entt::registry m_registry;
        std::vector<entt::entity> m_roots;
    };
}
