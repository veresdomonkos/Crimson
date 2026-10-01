#pragma once
#include "opengl_device.hpp"
#include "crimson/ui/imgui_backend.hpp"
#include "opengl_resource_manager.hpp"

namespace crimson::opengl
{
    class OpenGLImGuiBackend : public ImGuiBackend
    {
    public:
        OpenGLImGuiBackend(OpenGLDevice& device, OpenGLResourceManager& resourceManager);
        ~OpenGLImGuiBackend() override;
        void NewFrame() override;
        void RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles) override;
        ImTextureID GetOrCreateTextureId(TextureHandle texture) override;
    private:
        OpenGLResourceManager& m_resourceManager;
    };
}
