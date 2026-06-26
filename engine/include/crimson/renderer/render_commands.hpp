#pragma once
#include <cstdint>
#include <glm/glm.hpp>

#include "crimson/renderer/core.hpp"
#include "crimson/core/command_buffer.hpp"

namespace crimson
{
    enum class RendererCommandType : uint32_t
    {
        None,
        Clear, // LEGACY
        BeginRenderPass,
        EndRenderPass
    };

    using RenderCommandBuffer = CommandBuffer<RendererCommandType>;

#define REGISTER_RENDER_COMMAND(Type) static RendererCommandType GetStaticType() { return RendererCommandType::Type; } \

    struct ClearCommand
    {
        glm::vec4 Color;

        ClearCommand(const glm::vec4& color) : Color(color) {}

        REGISTER_RENDER_COMMAND(Clear)
    };

    struct BeginRenderPassCommand
    {
        RenderTargetHandle Target;
        ClearFlags ClearFlags;
        glm::vec4 ClearColor;
        float ClearDepth;
        uint32_t ClearStencil;

        explicit BeginRenderPassCommand(const RenderPassInfo& renderPassInfo)
            : Target(renderPassInfo.Target), ClearFlags(renderPassInfo.ClearFlags),
              ClearColor(renderPassInfo.ClearColor), ClearDepth(renderPassInfo.ClearDepth),
              ClearStencil(renderPassInfo.ClearStencil)
        {}

        REGISTER_RENDER_COMMAND(BeginRenderPass)
    };

    struct EndRenderPassCommand
    {
        REGISTER_RENDER_COMMAND(EndRenderPass)
    };
}
