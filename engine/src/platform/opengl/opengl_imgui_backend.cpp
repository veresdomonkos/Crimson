#include "opengl_imgui_backend.hpp"

#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include "opengl_resource_manager.hpp"

namespace crimson::opengl
{
    void OpenGLImGuiBackend::Init(const Renderer& handles, const Window& window)
    {
        ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window.GetNativeHandle()), true);
        ImGui_ImplOpenGL3_Init("#version 450");
    }

    void OpenGLImGuiBackend::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void OpenGLImGuiBackend::RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles)
    {
        ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }

    void OpenGLImGuiBackend::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }

    ImTextureID OpenGLImGuiBackend::GetOrCreateTextureId(ResourceManager &resourceManager, TextureHandle texture)
    {
        auto& glResMgr = static_cast<OpenGLResourceManager&>(resourceManager);
        const OpenGLTexture& tex = glResMgr.GetTexture(texture);
        return static_cast<ImTextureID>(tex.GLHandle);
    }
}
