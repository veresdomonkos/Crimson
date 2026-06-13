#include "crimson/renderer/renderer.hpp"

namespace crimson
{
    void Renderer::Initialize()
    {
        s_backend = RendererBackend::Create();
        s_commandQueue = new RenderCommandQueue(2 * 1024 * 1024);
    }

    void Renderer::Shutdown()
    {
        delete s_backend;
        delete s_commandQueue;
    }

    void Renderer::Clear(const glm::vec4& color)
    {
        s_commandQueue->Submit<ClearCommand>(color);
    }

    void Renderer::BeginScene()
    {
    }

    void Renderer::EndScene()
    {
        s_backend->Execute(*s_commandQueue);
        s_commandQueue->Clear();
    }
}