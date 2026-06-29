#pragma once
#include <cstdint>

namespace crimson
{
    template <typename Tag>
    class Handle
    {
    public:
        constexpr Handle() = default;

        constexpr explicit Handle(std::uint32_t id, std::uint32_t generation)
            : m_packed((static_cast<std::uint64_t>(generation) << 32) | id) {}

        [[nodiscard]] constexpr std::uint32_t GetId() const { return static_cast<std::uint32_t>(m_packed & 0xFFFFFFFF); }
        [[nodiscard]] constexpr std::uint32_t GetGeneration() const { return static_cast<std::uint32_t>(m_packed >> 32); }

        [[nodiscard]] constexpr std::uint64_t GetRaw() const { return m_packed; }

        [[nodiscard]] constexpr bool IsValid() const { return m_packed != 0; }
        constexpr explicit operator bool() const { return IsValid(); }

        constexpr bool operator==(const Handle& other) const = default;

        static constexpr Handle Invalid() { return Handle{}; }
    private:
        std::uint64_t m_packed = 0;
    };

    class RenderSurfaceTag {};
    class RenderTargetTag {};
    class VertexBufferTag {};
    class IndexBufferTag {};
    class ShaderTag {};

    using RenderSurfaceHandle = Handle<RenderSurfaceTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
    using VertexBufferHandle = Handle<VertexBufferTag>;
    using IndexBufferHandle = Handle<IndexBufferTag>;
    using ShaderHandle = Handle<ShaderTag>;
}