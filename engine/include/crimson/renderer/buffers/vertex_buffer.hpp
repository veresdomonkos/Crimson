#pragma once
#include <memory>

#include "crimson/renderer/render_core.hpp"

namespace crimson
{
    class VertexBuffer
    {
    public:
        virtual ~VertexBuffer() = default;
        bool operator=(const VertexBuffer&) const = delete;

        virtual void SetLayout(const BufferLayout& layout) = 0;
        virtual const BufferLayout& GetLayout() const = 0;
        virtual ResourceHandle GetHandle() const = 0;

        static std::shared_ptr<VertexBuffer> Create(float* vertices, size_t size);
    };
}
