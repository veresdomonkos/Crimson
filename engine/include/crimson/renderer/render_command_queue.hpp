#pragma once

#include <cstddef>
#include "render_commands.hpp"

namespace crimson
{


    class RenderCommandQueue
    {
    public:
        RenderCommandQueue(size_t bufferSize) : m_size(bufferSize)
        {
            m_bufferStart = new std::byte[bufferSize];
            m_writePtr = m_bufferStart;
        }

        ~RenderCommandQueue()
        {
            delete[] m_bufferStart;
        }


        template<typename T, typename... Args>
        void Submit(Args&&... args)
        {
            auto* header = reinterpret_cast<RenderCommandHeader*>(m_writePtr);
            header->Type = T::GetStaticType();
            header->Size = sizeof(T);

            auto* commandStorage = reinterpret_cast<T*>(header + 1);
            new (commandStorage) T(std::forward<Args>(args)...);
            m_writePtr += sizeof(RenderCommandHeader) + sizeof(T);
        }

        const std::byte* GetBufferStart() const { return m_bufferStart; }
        const std::byte* GetDataEnd() const { return m_writePtr; }

        void Clear() { m_writePtr = m_bufferStart; }

        class iterator
        {
        public:
            iterator(std::byte* ptr) : m_ptr(ptr) {}

            iterator& operator++()
            {
                m_ptr += sizeof(RenderCommandHeader) + reinterpret_cast<RenderCommandHeader*>(m_ptr)->Size;
                return *this;
            }

            RenderCommandView& operator*() const
            {
                return reinterpret_cast<RenderCommandView&>(*m_ptr);
            }

            bool operator!=(const iterator& other) const
            {
                return  m_ptr != other.m_ptr;
            }
        private:
            std::byte* m_ptr;
        };

        iterator begin() const { return m_bufferStart; }
        iterator end() const { return m_writePtr; }
    private:
        std::byte* m_bufferStart;
        std::byte* m_writePtr;
        size_t m_size;
    };
}
