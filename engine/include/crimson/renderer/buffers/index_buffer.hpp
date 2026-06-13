#pragma once
#include <memory>

#include "crimson/renderer/render_core.hpp"

namespace crimson
{
    class IndexBuffer
    {
    public:
        virtual ~IndexBuffer() = default;

        IndexBuffer& operator=(const IndexBuffer&) = delete;

        virtual uint32_t GetCount() const = 0;
        virtual ResourceHandle GetHandle() const = 0;

        static std::shared_ptr<IndexBuffer> Create(uint32_t* indices, uint32_t count);
    };
}
