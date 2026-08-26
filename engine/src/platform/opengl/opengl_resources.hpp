#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>

#include "vertex_array_cache.hpp"
#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/shader_property_info.hpp"

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

    struct OpenGLShader
    {
        GLuint GLHandle{};
        size_t UBOSize;
        std::unordered_map<std::string, ShaderPropertyInfo> Properties{};
    };

    struct OpenGLMaterial
    {
        ShaderHandle Shader = ShaderHandle::Invalid();

        std::unique_ptr<std::byte[]> UniformData = nullptr;
        std::size_t UniformDataSize = 0;
        GLuint GLBufferHandle = 0;

        //std::vector<TextureHandle> Textures;
        bool IsDirty = true;

        template<MaterialProperty T>
        void SetPropertyByOffset(std::size_t offset, const T& value)
        {
            *std::launder(reinterpret_cast<T*>(UniformData.get() + offset)) = value;
            IsDirty = true;
        }

        template<MaterialProperty T>
        [[nodiscard]] const T& GetPropertyByOffset(std::size_t offset) const
        {
            return *std::launder(reinterpret_cast<const T*>(UniformData.get() + offset));
        }
    };

    struct OpenglResourceTraits
    {
        using RenderSurface = OpenGLSurface;
        using RenderTarget = OpenGLRenderTarget;
        using VertexBuffer = OpenGLVertexBuffer;
        using IndexBuffer = OpenGLIndexBuffer;
        using Shader = OpenGLShader;
        using GraphicsPipeline = OpenGLGraphicsPipeline;
        using Material = OpenGLMaterial;
    };
}
