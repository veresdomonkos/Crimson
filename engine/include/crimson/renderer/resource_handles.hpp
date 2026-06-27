#pragma once
#include <cstdint>

namespace crimson
{
    class RenderSurfaceTag {};
    class RenderTargetTag {};

    template <typename Tag>
    class Handle
    {
    public:
        constexpr Handle() = default;

        constexpr explicit Handle(std::uint32_t id, std::uint32_t generation)
            : m_packed((static_cast<std::uint64_t>(generation) << 32) | id) {}

        constexpr std::uint32_t GetId() const { return static_cast<std::uint32_t>(m_packed & 0xFFFFFFFF); }
        constexpr std::uint32_t GetGeneration() const { return static_cast<std::uint32_t>(m_packed >> 32); }

        constexpr std::uint64_t GetRaw() const { return m_packed; }

        constexpr bool IsValid() const { return m_packed != 0; }
        constexpr explicit operator bool() const { return IsValid(); }

        constexpr bool operator==(const Handle& other) const = default;

        static constexpr Handle Invalid() { return Handle{}; }
    private:
        std::uint64_t m_packed = 0;
    };

    using RenderSurfaceHandle = Handle<RenderSurfaceTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
}