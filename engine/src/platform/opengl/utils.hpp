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
}
