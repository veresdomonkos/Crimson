#pragma once
#include "renderer_backend.hpp"

namespace crimson
{
    enum class RendererAPI
    {
        None,
        OpenGL
    };

    class Renderer
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Clear(const glm::vec4& color);
        static void BeginScene();
        static void EndScene();

        static RendererAPI API() { return RendererAPI::OpenGL; }
    private:
        inline static RendererBackend* s_backend = nullptr;
        inline static RenderCommandQueue* s_commandQueue = nullptr;
    };
}
