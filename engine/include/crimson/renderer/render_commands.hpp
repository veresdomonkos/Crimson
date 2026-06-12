#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace crimson
{
    enum class RendererCommandType : uint32_t
    {
        None,
        Clear
    };

    struct RenderCommandHeader
    {
         RendererCommandType Type;
         uint32_t Size;
    };

#define REGISTER_RENDER_COMMAND(Type) static RendererCommandType GetStaticType() { return RendererCommandType::Type; } \

    struct ClearCommand
    {
        glm::vec4 Color;

        ClearCommand(const glm::vec4& color) : Color(color) {}

        REGISTER_RENDER_COMMAND(Clear)
    };
}
