#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <cassert>
#include <type_traits>

namespace crimson
{
    template <typename T>
    concept CommandTypeEnum = std::is_enum_v<T>;

    template <CommandTypeEnum CommandType>
    struct CommandTraits
    {
        struct alignas(std::max_align_t) Header
        {
            CommandType Type;
            uint32_t Size;
        };

        class View
        {
        public:
            View() = delete;
            explicit View(const Header* header) : m_header(header) {}

            CommandType GetType() const
            {
                return m_header->Type;
            }

            template <typename T>
            const T& As() const
            {
                assert(m_header->Type == T::GetStaticType());
                return *std::launder(reinterpret_cast<const T*>(m_header + 1));
            }
        private:
            const Header* m_header;
        };
    };

    constexpr size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    template <CommandTypeEnum CommandType>
    class CommandBuffer
    {
    public:
        using Header = CommandTraits<CommandType>::Header;
        using CommandView = CommandTraits<CommandType>::View;
    public:
        explicit CommandBuffer(size_t bufferSize) : m_size(bufferSize)
        {
            m_bufferStart = static_cast<std::byte*>(::operator new[](bufferSize, std::align_val_t(alignof(std::max_align_t))));
            m_writePtr = m_bufferStart;
        }

        CommandBuffer(const CommandBuffer& other) = delete;
        CommandBuffer& operator=(const CommandBuffer& other) = delete;

        ~CommandBuffer()
        {
            ::operator delete[](m_bufferStart, std::align_val_t(alignof(std::max_align_t)));
        }

        template<typename T, typename... Args>
        void Submit(Args&&... args)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(std::is_trivially_destructible_v<T>);
            static_assert(alignof(T) <= alignof(std::max_align_t));

            const size_t entrySize = AlignUp(sizeof(Header) + sizeof(T), alignof(std::max_align_t));
            assert(m_writePtr + entrySize <= m_bufferStart + m_size);

            auto* header = reinterpret_cast<Header*>(m_writePtr);
            header->Type = T::GetStaticType();
            header->Size = sizeof(T);

            auto* commandStorage = reinterpret_cast<T*>(header + 1);
            new (commandStorage) T(std::forward<Args>(args)...);
            m_writePtr += entrySize;
        }

        void Clear() { m_writePtr = m_bufferStart; }

        class iterator
        {
        public:
            iterator(std::byte* ptr) : m_ptr(ptr) {}

            iterator& operator++()
            {
                m_ptr += AlignUp(sizeof(Header) + reinterpret_cast<Header*>(m_ptr)->Size, alignof(std::max_align_t));
                return *this;
            }

            CommandView operator*() const
            {
                return CommandView(reinterpret_cast<const Header*>(m_ptr));
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
