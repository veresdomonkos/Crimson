#include "crimson/ui/imgui_backend.hpp"

#include "crimson/renderer/renderer_api.hpp"
#include "../platform/opengl/opengl_imgui_backend.hpp"
#include "../platform/vulkan/vulkan_imgui_backend.hpp"

namespace crimson
{
    std::unique_ptr<ImGuiBackend> ImGuiBackend::Create()
    {
        switch (RendererAPI::GetType())
        {
            case RendererAPIType::Vulkan: return  std::make_unique<vulkan::VulkanImGuiBackend>();
            case RendererAPIType::OpenGL: return std::make_unique<opengl::OpenGLImGuiBackend>();
        }

        return nullptr;
    }
}
