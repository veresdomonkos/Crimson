#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace crimson
{
    template <typename T>
    void Append(std::string& bytes, T value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    inline void AppendString(std::string& bytes, std::string_view text)
    {
        Append<uint64_t>(bytes, text.size());
        bytes.append(text);
    }

    // Reads from bytes without going past them. Once something doesn't fit, everything after it fails too.
    class Reader
    {
    public:
        explicit Reader(std::string_view bytes) : m_bytes(bytes) {}

        template <typename T>
        bool Read(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (m_bytes.size() < sizeof(T))
                return Fail();

            std::memcpy(&value, m_bytes.data(), sizeof(T));
            m_bytes.remove_prefix(sizeof(T));
            return true;
        }

        // The size is checked against what is left before anything is allocated, as it can be anything
        bool ReadString(std::string& text)
        {
            uint64_t size = 0;
            if (!Read(size) || size > m_bytes.size())
                return Fail();

            text.assign(m_bytes.data(), static_cast<std::size_t>(size));
            m_bytes.remove_prefix(static_cast<std::size_t>(size));
            return true;
        }

        [[nodiscard]] std::size_t Remaining() const { return m_bytes.size(); }
        [[nodiscard]] bool AtEnd() const { return m_bytes.empty(); }

    private:
        bool Fail()
        {
            m_bytes = {};
            return false;
        }

        std::string_view m_bytes;
    };
}
