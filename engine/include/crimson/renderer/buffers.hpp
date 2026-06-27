#pragma once

#include "crimson/renderer/buffer_layout.hpp"

namespace crimson
{
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