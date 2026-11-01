#pragma once
#include <unordered_map>
#include <glad/glad.h>

#include "crimson/renderer/resource_handles.hpp"

namespace crimson::opengl
{
    struct VertexArrayKey
    {
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle IndexBuffer;

        bool operator==(const VertexArrayKey&) const = default;
    };

    struct VertexArrayKeyHash
    {
        size_t operator()(const VertexArrayKey& key) const
        {
            size_t hash = 0;

            HashCombine(hash, key.VertexBuffer.GetRaw());
            HashCombine(hash, key.IndexBuffer.GetRaw());

            return hash;
        }
    private:
        static void HashCombine(size_t& seed, size_t value)
        {
            seed ^= value
                + 0x9e3779b9
                + (seed << 6)
                + (seed >> 2);
        }
    };

    using VertexArrayCache = std::unordered_map<VertexArrayKey, GLuint, VertexArrayKeyHash>;
}
