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

    void Renderer::DrawIndexed(const std::shared_ptr<VertexBuffer>& vertexBuffer, const std::shared_ptr<IndexBuffer>& indexBuffer)
    {
        s_commandQueue->Submit<DrawIndexedCommand>(vertexBuffer->GetHandle(), indexBuffer->GetHandle(), 0, indexBuffer->GetCount());
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