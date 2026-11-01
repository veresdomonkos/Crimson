#pragma once
#include <cassert>
#include <utility>

#include "crimson/scene/components.hpp"
#include "crimson/scene/scene.hpp"

namespace crimson
{
    // A cheap handle to compare and copy around. It doesn't own the entity and turns invalid when it is destroyed.
    class Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene) : m_handle(handle), m_scene(scene) {}

        template <typename T, typename... Args>
        T& AddComponent(Args&&... args) const
        {
            assert(!HasComponent<T>());
            return m_scene->GetRegistry().emplace<T>(m_handle, std::forward<Args>(args)...);
        }

        template <typename T>
        [[nodiscard]] T& GetComponent() const
        {
            assert(HasComponent<T>());
            return m_scene->GetRegistry().get<T>(m_handle);
        }

        template <typename T>
        [[nodiscard]] T* TryGetComponent() const
        {
            return m_scene->GetRegistry().try_get<T>(m_handle);
        }

        template <typename T>
        [[nodiscard]] bool HasComponent() const
        {
            return m_scene->GetRegistry().all_of<T>(m_handle);
        }

        template <typename T>
        void RemoveComponent() const
        {
            m_scene->GetRegistry().remove<T>(m_handle);
        }

        [[nodiscard]] entt::entity GetHandle() const { return m_handle; }

        explicit operator bool() const { return m_scene && m_scene->GetRegistry().valid(m_handle); }
        bool operator==(const Entity& other) const { return m_handle == other.m_handle && m_scene == other.m_scene; }
    private:
        entt::entity m_handle = entt::null;
        Scene* m_scene = nullptr;
    };
}
