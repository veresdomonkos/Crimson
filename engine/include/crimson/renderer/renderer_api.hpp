#pragma once

namespace crimson
{
    enum class RendererAPIType
    {
        None,
        OpenGL
    };

    class RendererAPI
    {
    public:
        static RendererAPIType GetType() { return  RendererAPIType::OpenGL; }
    };
}