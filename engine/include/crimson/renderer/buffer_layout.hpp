#pragma once
#include <cstdint>
#include <vector>

namespace crimson
{
    enum class ShaderDataType
    {
        Float, Float2, Float3, Float4,
        Int, Int2, Int3, Int4
    };

    namespace ShaderData
    {
        static constexpr uint32_t Size(ShaderDataType type)
        {
            switch(type)
            {
                case ShaderDataType::Float:  return 4;
                case ShaderDataType::Float2: return 8;
                case ShaderDataType::Float3: return 12;
                case ShaderDataType::Float4: return 16;

                case ShaderDataType::Int:  return 4;
                case ShaderDataType::Int2: return 8;
                case ShaderDataType::Int3: return 12;
                case ShaderDataType::Int4: return 16;

                default: return 0;
            }
        }

        static constexpr uint32_t ComponentCount(ShaderDataType type)
        {
            switch(type)
            {
                case ShaderDataType::Float:  return 1;
                case ShaderDataType::Float2: return 2;
                case ShaderDataType::Float3: return 3;
                case ShaderDataType::Float4: return 4;

                case ShaderDataType::Int:  return 1;
                case ShaderDataType::Int2: return 2;
                case ShaderDataType::Int3: return 3;
                case ShaderDataType::Int4: return 4;

                default: return 0;
            }
        }
    }


    struct BufferElement
    {
        ShaderDataType Type;
        uint32_t Offset = 0;
        uint32_t Size = 0;

        BufferElement(ShaderDataType type) : Type(type), Size(ShaderData::Size(type)) {}

        [[nodiscard]] uint32_t ComponentCount() const { return ShaderData::ComponentCount(Type); }

        bool operator==(const BufferElement&) const = default;
    };

    class BufferLayout
    {
    public:
        BufferLayout() = default;

        BufferLayout(std::initializer_list<BufferElement> elements)
            : m_elements(elements)
        {
            CalculateOffsets();
        }

        [[nodiscard]] std::vector<BufferElement>::const_iterator begin() const { return m_elements.begin(); }
        [[nodiscard]] std::vector<BufferElement>::const_iterator end() const { return m_elements.end(); }
        [[nodiscard]] uint32_t GetStride() const { return m_stride; }

        bool operator==(const BufferLayout&) const = default;
    private:
        void CalculateOffsets()
        {
            uint32_t offset = 0;
            m_stride = 0;

            for(auto& element : m_elements)
            {
                element.Offset = offset;

                offset += element.Size;
                m_stride += element.Size;
            }
        }
    private:
        std::vector<BufferElement> m_elements;
        uint32_t m_stride = 0;
    };
}
