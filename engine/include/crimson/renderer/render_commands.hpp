#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include "crimson/core/command_buffer.hpp"

namespace crimson
{
    enum class RendererCommandType : uint32_t
    {
        None,
        Clear
    };

    using RenderCommandBuffer = CommandBuffer<RendererCommandType>;

#define REGISTER_RENDER_COMMAND(Type) static RendererCommandType GetStaticType() { return RendererCommandType::Type; } \

    struct ClearCommand
    {
        glm::vec4 Color;

        ClearCommand(const glm::vec4& color) : Color(color) {}

        REGISTER_RENDER_COMMAND(Clear)
    };
}
