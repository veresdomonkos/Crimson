#pragma once

#include <optional>
#include <vector>
#include <glad/glad.h>
#include "crimson/renderer/resource_handles.hpp"

namespace crimson::opengl
{
    struct OpenGLSurface
    {
        void* WindowHandle{};
        RenderTargetHandle BackBufferHandle;
    };

    struct OpenGLImage
    {
        GLuint Texture = 0;
        GLenum Format = GL_RGBA8;
    };

    struct OpenGLRenderTarget
    {
        uint32_t Width;
        uint32_t Height;
        GLuint FrameBufferHandle;
        std::vector<OpenGLImage> Colors;
        std::optional<OpenGLImage> Depth;
    };

    struct OpenglResourceTraits
    {
        using RenderSurface = OpenGLSurface;
        using RenderTarget = OpenGLRenderTarget;
    };
}
