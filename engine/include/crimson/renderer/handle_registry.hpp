#pragma once

#include <cassert>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <cstdint>

namespace crimson
{
    template <typename Handle, typename T>
    class HandleRegistry
    {
    public:
        static_assert(std::is_constructible_v<Handle, std::uint32_t>,
                      "HandleRegistry requires a handle type constructible from uint32_t");

        HandleRegistry() = default;

        template <typename U>
        Handle Register(U&& value)
        {
            static_assert(std::is_constructible_v<T, U&&>,
                          "Value must be constructible as T");

            const Handle handle(++m_counter);
            auto [it, inserted] = m_lookup.emplace(handle, std::forward<U>(value));
            assert(inserted);
            (void)it;
            return handle;
        }

        bool Contains(const Handle& handle) const
        {
            return m_lookup.contains(handle);
        }

        T& Get(const Handle& handle)
        {
            auto it = m_lookup.find(handle);
            assert(it != m_lookup.end());
            return it->second;
        }

        const T& Get(const Handle& handle) const
        {
            auto it = m_lookup.find(handle);
            assert(it != m_lookup.end());
            return it->second;
        }

        T* Find(const Handle& handle)
        {
            auto it = m_lookup.find(handle);
            return it == m_lookup.end() ? nullptr : &it->second;
        }

        const T* Find(const Handle& handle) const
        {
            auto it = m_lookup.find(handle);
            return it == m_lookup.end() ? nullptr : &it->second;
        }

        void Clear()
        {
            m_lookup.clear();
            m_counter = 0;
        }

    private:
        std::unordered_map<Handle, T> m_lookup;
        std::uint32_t m_counter = 0;
    };
}
