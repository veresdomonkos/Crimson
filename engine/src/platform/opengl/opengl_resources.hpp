#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <new>
#include <glad/glad.h>

#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/buffer_layout.hpp"
#include "crimson/renderer/material_settings.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/shader_variant.hpp"
#include "crimson/renderer/shader_property_info.hpp"
#include "shader/shader_reflection.hpp"

namespace crimson::opengl
{
    struct OpenGLSurface
    {
        void* WindowHandle{};
        RenderTargetHandle BackBufferHandle;
    };

    struct OpenGLTexture
    {
        GLuint GLHandle = 0;
        GLenum InternalFormat = GL_RGBA8;
        uint32_t Width  = 0;
        uint32_t Height = 0;
    };

    struct OpenGLRenderTarget
    {
        std::vector<TextureHandle> ColorAttachments;
        TextureHandle DepthAttachment;
        uint32_t Width  = 0;
        uint32_t Height = 0;
        GLuint FrameBufferHandle = 0;
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

    // A texture unit the shader samples from, with what has to be bound to it
    struct OpenGLShaderTextureUnit
    {
        std::string Texture;
        GLuint Sampler = 0;
        uint32_t Unit = 0;
        bool IsShadowAtlas = false;
    };

    struct OpenGLProgram
    {
        GLuint GLHandle = 0;
        GLint ObjectIndexLocation = -1;
        GLint ViewIndexLocation = -1; // also -1 for a shader that doesn't use the view
        std::vector<OpenGLShaderTextureUnit> TextureUnits;
    };

    struct OpenGLShader
    {
        OpenGLProgram Main;
        OpenGLProgram Depth; // VSMain with PSDepth, or alone, for the passes that only need depth

        ShaderInterface Interface;

        [[nodiscard]] const OpenGLProgram& For(ShaderVariant pass) const { return pass == ShaderVariant::Depth ? Depth : Main; }
    };

    struct OpenGLMaterial
    {
        ShaderHandle Shader = ShaderHandle::Invalid();
        MaterialSettings Settings;

        std::unique_ptr<std::byte[]> UniformData = nullptr;
        std::size_t UniformDataSize = 0;
        GLuint GLBufferHandle = 0;
        std::unordered_map<std::string, GLuint> Textures;
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
        using Material = OpenGLMaterial;
        using Texture = OpenGLTexture;
    };
}
