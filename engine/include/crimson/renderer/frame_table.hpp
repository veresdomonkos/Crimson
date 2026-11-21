#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace crimson
{
    // The id of something recorded into one frame: an index into that frame's table. A default constructed id is none, and
    // what none stands for is up to what owns the kind: the identity for objects, the pass's own for views, the material's
    // own for material properties. Unlike a Handle it has no generation and lives for one frame, and the Tag makes the
    // kinds different types, so they can't be mixed up.
    template <typename Tag>
    class FrameId
    {
    public:
        constexpr FrameId() = default;
        constexpr explicit FrameId(uint32_t index) : m_index(index) {}

        [[nodiscard]] constexpr bool IsValid() const { return m_index != std::numeric_limits<uint32_t>::max(); }
        constexpr explicit operator bool() const { return IsValid(); }

        [[nodiscard]] constexpr uint32_t Index() const
        {
            assert(IsValid() && "A FrameId of none has no index");
            return m_index;
        }

        constexpr bool operator==(const FrameId&) const = default;
    private:
        uint32_t m_index = std::numeric_limits<uint32_t>::max();
    };

    // What one frame recorded of a kind, in the order it was added. Clear starts it over for the next frame and keeps the
    // capacity. It has no free list, as nothing is removed from a frame, which is what sets it apart from a HandleRegistry.
    template <typename Tag, typename T>
    class FrameTable
    {
    public:
        using Id = FrameId<Tag>;

        Id Add(const T& value)
        {
            m_values.push_back(value);
            return Id{ static_cast<uint32_t>(m_values.size() - 1) };
        }

        [[nodiscard]] bool Contains(Id id) const { return id.IsValid() && id.Index() < m_values.size(); }

        [[nodiscard]] const T& Get(Id id) const
        {
            assert(Contains(id) && "The id has to come from this frame");
            return m_values[id.Index()];
        }

        // Everything added, for the uploads. An id's index is its place in it.
        [[nodiscard]] std::span<const T> All() const { return m_values; }
        [[nodiscard]] uint32_t Size() const { return static_cast<uint32_t>(m_values.size()); }

        void Clear() { m_values.clear(); }
    private:
        std::vector<T> m_values;
    };
}
