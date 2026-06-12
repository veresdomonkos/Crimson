#pragma once

#include <cstddef>
#include <cstdint>

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
    private:
        std::byte* m_bufferStart;
        std::byte* m_writePtr;
        size_t m_size;
    };
}
