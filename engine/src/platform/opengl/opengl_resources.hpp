#pragma once

#include <glad/glad.h>
#include "crimson/renderer/resource_handles.hpp"

namespace crimson::opengl
{
    struct OpenGLSurface
    {
        void* WindowHandle{};
        RenderTargetHandle BackBufferHandle;
    };

    struct OpenGLRenderTarget
    {
        uint32_t Width;
        uint32_t Height;
        GLuint FrameBufferHandle;
    };

    struct OpenglResourceTraits
    {
        using Surface = OpenGLSurface;
        using RenderTarget = OpenGLRenderTarget;
    };
}
