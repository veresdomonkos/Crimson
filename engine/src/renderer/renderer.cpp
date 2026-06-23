#include "crimson/renderer/renderer.hpp"

#include "../platform/opengl/opengl_renderer.hpp"
#include "../platform/vulkan/vulkan_renderer.hpp"
#include "crimson/core/core.hpp"
#include "crimson/core/log.hpp"
#include "crimson/renderer/renderer_api.hpp"

namespace crimson
{
    Unique<Renderer> Renderer::Create()
    {
        switch (RendererAPI::GetType())
        {
            case RendererAPIType::OpenGL:
                return CreateUnique<opengl::OpenGLRenderer>();
            case RendererAPIType::Vulkan:
                return CreateUnique<vulkan::VulkanRenderer>();
            default:
                LOG_ERROR("Unknown RendererAPI type");
                return nullptr;
        }
    }
}
