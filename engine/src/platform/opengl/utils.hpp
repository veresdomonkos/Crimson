#pragma once
#include "crimson/renderer/core.hpp"
#include <glad/glad.h>

namespace crimson::opengl::utils
{
    static constexpr GLenum GetGLBufferUsage(BufferUsage usage)
    {
        switch(usage)
        {
            case BufferUsage::Static:
                return GL_STATIC_DRAW;

            case BufferUsage::Dynamic:
                return GL_DYNAMIC_DRAW;

            case BufferUsage::Stream:
                return GL_STREAM_DRAW;
        }

        return GL_STATIC_DRAW;
    }

    static constexpr size_t GetGLIndexType(IndexType type)
    {
        switch (type)
        {
            case IndexType::UInt16: return GL_UNSIGNED_SHORT;
            case IndexType::UInt32: return GL_UNSIGNED_INT;
        }

        return 0;
    }

    static constexpr std::size_t GetGLTypeSize(GLenum type)
    {
        switch (type)
        {
            case GL_FLOAT:             return sizeof(float);
            case GL_FLOAT_VEC2:        return sizeof(float) * 2;
            case GL_FLOAT_VEC3:        return sizeof(float) * 3;
            case GL_FLOAT_VEC4:        return sizeof(float) * 4;
            case GL_INT:               return sizeof(int);
            case GL_INT_VEC2:          return sizeof(int) * 2;
            case GL_INT_VEC3:          return sizeof(int) * 3;
            case GL_INT_VEC4:          return sizeof(int) * 4;
            case GL_BOOL:              return sizeof(bool);
            case GL_FLOAT_MAT3:        return sizeof(float) * 9;
            case GL_FLOAT_MAT4:        return sizeof(float) * 16;
            default:                   return 0;
        }
    }

    static constexpr GLenum GetGLInternalFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGBA8:           return GL_RGBA8;
            case TextureFormat::RGB8:            return GL_RGB8;
            case TextureFormat::R8:              return GL_R8;
            case TextureFormat::RGBA16F:         return GL_RGBA16F;
            case TextureFormat::RGBA32F:         return GL_RGBA32F;
            case TextureFormat::Depth24Stencil8: return GL_DEPTH24_STENCIL8;
            case TextureFormat::Depth32F:        return GL_DEPTH_COMPONENT32F;
        }
        return GL_RGBA8;
    }

    static constexpr GLenum GetGLUploadFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGB8:  return GL_RGB;
            case TextureFormat::R8:    return GL_RED;
            default:                   return GL_RGBA;
        }
    }

    static constexpr GLenum GetGLUploadType(TextureFormat format)
    {
        return (format == TextureFormat::RGBA16F || format == TextureFormat::RGBA32F)
            ? GL_FLOAT : GL_UNSIGNED_BYTE;
    }

    static constexpr bool HasStencil(TextureFormat format)
    {
        return format == TextureFormat::Depth24Stencil8;
    }
}
