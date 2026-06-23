#pragma once
#include <cstdint>
#include <functional>

namespace crimson
{
    class RenderSurfaceTag {};
    class RenderTargetTag {};

    template <typename Tag>
    struct Handle
    {
        Handle() = default;
        explicit Handle(uint32_t id) : Id(id), Generation(0) {}

        uint32_t Id = 0;
        uint32_t Generation = 0;

        friend bool operator==(const Handle& a, const Handle& b)
        {
            return a.Id == b.Id && a.Generation == b.Generation;
        }

        static constexpr Handle Invalid() { return Handle{}; }
    };

    using RenderSurfaceHandle = Handle<RenderSurfaceTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
}

template <typename Tag>
struct std::hash<crimson::Handle<Tag>>
{
    size_t operator()(const crimson::Handle<Tag>& h) const noexcept
    {
        size_t seed = std::hash<uint32_t>{}(h.Id);
        seed ^= std::hash<uint32_t>{}(h.Generation) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};