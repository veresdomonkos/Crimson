#pragma once
#include <cstdint>
#include <glm/glm.hpp>

#include "render_core.hpp"

namespace crimson
{
    enum class RendererCommandType : uint32_t
    {
        None,
        Clear,
        DrawIndexed
    };

    struct RenderCommandHeader
    {
         RendererCommandType Type;
         uint32_t Size;
    };

    class RenderCommandView
    {
    public:
        RendererCommandType GetType() const { return m_header.Type; }

        template <typename T>
        const T& As() const
        {
            assert(m_header.Type == T::GetStaticType());
            return reinterpret_cast<const T&>(*(&m_header + 1));
        }
    private:
        RenderCommandHeader m_header;
    };

#define REGISTER_RENDER_COMMAND(Type) static RendererCommandType GetStaticType() { return RendererCommandType::Type; } \

    struct ClearCommand
    {
        glm::vec4 Color;

        ClearCommand(const glm::vec4& color) : Color(color) {}

        REGISTER_RENDER_COMMAND(Clear)
    };

    struct DrawIndexedCommand
    {
        ResourceHandle VertexBufferHandle;
        ResourceHandle IndexBufferHandle;
        ResourceHandle ShaderHandle;
        uint32_t IndexCount;

        DrawIndexedCommand(ResourceHandle vbo, ResourceHandle ibo, ResourceHandle shader, uint32_t indexCount)
            : VertexBufferHandle(vbo)
            , IndexBufferHandle(ibo)
            , ShaderHandle(shader)
            , IndexCount(indexCount) {}

        REGISTER_RENDER_COMMAND(DrawIndexed)
    };
}
