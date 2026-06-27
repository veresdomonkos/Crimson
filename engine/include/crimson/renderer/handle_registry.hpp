#pragma once

#include <cassert>
#include <type_traits>
#include <vector>
#include <utility>
#include <cstdint>
#include <optional>

namespace crimson
{
    template <typename Handle, typename T>
    class HandleRegistry
    {
    private:
        struct Slot
        {
            std::optional<T> Value;
            std::uint32_t Generation = 1;
            bool Occupied = false;
        };

    public:
        static_assert(std::is_constructible_v<Handle, std::uint32_t, std::uint32_t>,
                      "HandleRegistry requires a handle type constructible from (uint32_t id, uint32_t generation)");

        HandleRegistry() = default;

        template <typename U>
        Handle Register(U&& value)
        {
            static_assert(std::is_constructible_v<T, U&&>, "Value must be constructible as T");

            std::uint32_t index = 0;
            std::uint32_t generation = 1;

            if (!m_freeSlots.empty())
            {
                index = m_freeSlots.back();
                m_freeSlots.pop_back();

                auto& slot = m_slots[index];
                slot.Value.emplace(std::forward<U>(value));
                slot.Occupied = true;
                generation = slot.Generation;
            }
            else
            {
                index = static_cast<std::uint32_t>(m_slots.size());
                auto& slot = m_slots.emplace_back();
                slot.Value.emplace(std::forward<U>(value));
                slot.Occupied = true;
                generation = slot.Generation;
            }

            return Handle{index, generation};
        }

        void Unregister(const Handle& handle)
        {
            if (!Contains(handle)) return;

            const std::uint32_t id = handle.GetId();
            auto& slot = m_slots[id];
            slot.Value.reset();
            slot.Occupied = false;

            ++slot.Generation;
            if (slot.Generation == 0) [[unlikely]]
            {
                slot.Generation = 1;
            }

            m_freeSlots.push_back(id);
        }

        bool Contains(const Handle& handle) const
        {
            const std::uint32_t id = handle.GetId();
            if (id >= m_slots.size()) return false;

            const auto& slot = m_slots[id];
            return slot.Occupied && slot.Generation == handle.GetGeneration();
        }

        T& Get(const Handle& handle)
        {
            assert(Contains(handle));
            return *(m_slots[handle.GetId()].Value);
        }

        const T& Get(const Handle& handle) const
        {
            assert(Contains(handle));
            return *(m_slots[handle.GetId()].Value);
        }

        T* Find(const Handle& handle)
        {
            return Contains(handle) ? &*(m_slots[handle.GetId()].Value) : nullptr;
        }

        const T* Find(const Handle& handle) const
        {
            return Contains(handle) ? &*(m_slots[handle.GetId()].Value) : nullptr;
        }

        void Clear()
        {
            m_slots.clear();
            m_freeSlots.clear();
        }

        class iterator
        {
            using vec_iter = std::vector<Slot>::iterator;

        public:

            iterator(vec_iter it, vec_iter end)
                : m_it(it), m_end(end)
            {
                skipInvalid();
            }


            iterator& operator++()
            {
                ++m_it;
                skipInvalid();
                return *this;
            }


            T& operator*()
            {
                return m_it->Value.value();
            }


            bool operator==(const iterator& other) const
            {
                return m_it == other.m_it;
            }
        private:

            void skipInvalid()
            {
                while(
                    m_it != m_end &&
                    !m_it->Occupied
                )
                {
                    ++m_it;
                }
            }

            vec_iter m_it;
            vec_iter m_end;
        };

        iterator begin() { return iterator(m_slots.begin(), m_slots.end()); }
        iterator end() { return iterator(m_slots.end(), m_slots.end()); }
    private:
        std::vector<Slot> m_slots;
        std::vector<std::uint32_t> m_freeSlots;
    };
}