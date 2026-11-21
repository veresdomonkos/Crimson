#pragma once

#include "crimson/renderer/buffer_layout.hpp"

namespace crimson
{
    // A Static buffer never changes after it was created. A Dynamic or Stream buffer can be updated, once a frame or more:
    // Dynamic for what changes now and then, Stream for what is new every frame.
    enum class BufferUsage
    {
        Static,
        Dynamic,
        Stream
    };

    struct VertexBufferInfo
    {
        BufferLayout Layout;
        size_t Size{};
        BufferUsage Usage{};
    };

    enum class IndexType
    {
        UInt16,
        UInt32
    };

    class Index
    {
    public:
        static constexpr size_t Size(IndexType type)
        {
            switch (type)
            {
                case IndexType::UInt16: return 2;
                case IndexType::UInt32: return 4;
            }

            return 0;
        }
    };

    struct IndexBufferInfo
    {
        size_t Size{};
        BufferUsage Usage{};
        IndexType Type{};
    };
}
