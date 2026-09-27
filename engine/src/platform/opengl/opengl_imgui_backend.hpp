#pragma once
#include "crimson/ui/imgui_backend.hpp"

namespace crimson::opengl
{
    class OpenGLImGuiBackend : public ImGuiBackend
    {
    public:
        void Init(const Renderer& handles, const Window& window) override;
        void NewFrame() override;
        void RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles) override;
        void Shutdown() override;
        ImTextureID GetOrCreateTextureId(ResourceManager& resourceManager, TextureHandle texture) override;
    };
}
