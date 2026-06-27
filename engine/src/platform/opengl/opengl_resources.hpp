#pragma once

#include <optional>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>

#include "vertex_array_cache.hpp"
#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/buffer_layout.hpp"
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
        std::vector<OpenGLImage> Colors;
        std::optional<OpenGLImage> Depth;
        uint32_t Width;
        uint32_t Height;
        GLuint FrameBufferHandle;
    };

    struct OpenGLVertexBuffer
    {
        BufferLayout Layout;
        size_t Size{};
        GLuint GLHandle{};
        BufferUsage Usage{};
    };

    struct OpenGLIndexBuffer
    {
        size_t Size{};
        GLuint GLHandle{};
        BufferUsage Usage{};
        IndexType Type{};
    };

    struct VertexArray
    {
        GLuint GLHandle{};
    };
    
    struct OpenGLGraphicsPipeline
    {
        VertexArrayCache VAOCache;
    };

    struct OpenglResourceTraits
    {
        using RenderSurface = OpenGLSurface;
        using RenderTarget = OpenGLRenderTarget;
        using VertexBuffer = OpenGLVertexBuffer;
        using IndexBuffer = OpenGLIndexBuffer;
        using GraphicsPipeline = OpenGLGraphicsPipeline;
    };
}
