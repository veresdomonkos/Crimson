#pragma once

namespace crimson
{
    enum class RendererAPIType
    {
        None,
        OpenGL,
        Vulkan
    };

    class RendererAPI
    {
    public:
        static void Init(RendererAPIType type) {  s_type = type; }
        static RendererAPIType GetType() { return s_type; }
    private:
        inline static RendererAPIType s_type = RendererAPIType::OpenGL;
    };
}