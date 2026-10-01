#include "opengl_imgui_backend.hpp"

#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include "opengl_resource_manager.hpp"

namespace crimson::opengl
{
    OpenGLImGuiBackend::OpenGLImGuiBackend(OpenGLDevice& device, OpenGLResourceManager& resourceManager)
        : m_resourceManager(resourceManager)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui_ImplGlfw_InitForOpenGL(device.GetPrimaryWindow(), true);
        ImGui_ImplOpenGL3_Init("#version 450");
    }

    OpenGLImGuiBackend::~OpenGLImGuiBackend()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }

    void OpenGLImGuiBackend::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void OpenGLImGuiBackend::RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles)
    {
        ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }


    ImTextureID OpenGLImGuiBackend::GetOrCreateTextureId(TextureHandle texture)
    {
        const OpenGLTexture& tex = m_resourceManager.GetTexture(texture);
        return static_cast<ImTextureID>(tex.GLHandle);
    }
}
