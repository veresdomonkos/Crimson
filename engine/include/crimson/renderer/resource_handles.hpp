#pragma once
#include <cstdint>
#include "crimson/renderer/resource_tags.hpp"

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

    template <typename Tag>
    struct HandleHash
    {
        std::size_t operator()(const Handle<Tag>& handle) const noexcept
        {
            std::uint64_t x = handle.GetRaw();
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            return static_cast<std::size_t>(x);
        }
    };

    using RenderSurfaceHandle = Handle<RenderSurfaceTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
    using VertexBufferHandle = Handle<VertexBufferTag>;
    using IndexBufferHandle = Handle<IndexBufferTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using MaterialHandle = Handle<MaterialTag>;
    using TextureHandle = Handle<TextureTag>;
}