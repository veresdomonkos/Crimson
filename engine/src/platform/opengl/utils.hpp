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
}
