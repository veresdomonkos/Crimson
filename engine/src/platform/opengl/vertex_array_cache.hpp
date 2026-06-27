#pragma once
#include <unordered_map>

#include "crimson/renderer/resource_handles.hpp"

namespace crimson::opengl
{
    struct VertexArrayInfo
    {
        VertexBufferHandle VertexBuffer;
        IndexBufferHandle IndexBuffer;

        bool operator==(const VertexArrayInfo&) const = default;
    };

    struct VertexArrayInfoHash
    {
        size_t operator()(const VertexArrayInfo& info) const
        {
            size_t hash = 0;

            HashCombine(hash, info.VertexBuffer.GetRaw());
            HashCombine(hash, info.IndexBuffer.GetRaw());

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

    class VertexArrayTag {};
    using VertexArrayHandle = Handle<VertexArrayTag>;

    using VertexArrayCache = std::unordered_map<VertexArrayInfo, VertexArrayHandle, VertexArrayInfoHash>;
}
