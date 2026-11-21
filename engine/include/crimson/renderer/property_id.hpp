#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "crimson/core/hash.hpp"

namespace crimson
{
    // The identity of a material property or texture: the Fnv1a of its name, like Unity's Shader.PropertyToID. This is the one
    // way a property is named in the engine, by a shader's interface and by a draw's MaterialProperties.
    class PropertyId
    {
    public:
        constexpr explicit PropertyId(std::string_view name) : m_value(Fnv1a(name)) {}

        [[nodiscard]] constexpr uint64_t Value() const { return m_value; }

        constexpr bool operator==(const PropertyId&) const = default;
    private:
        uint64_t m_value;
    };

    struct PropertyIdHash
    {
        std::size_t operator()(const PropertyId& id) const { return static_cast<std::size_t>(id.Value()); }
    };
}
