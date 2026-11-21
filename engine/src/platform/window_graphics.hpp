#pragma once

#include <vector>

#include "glm/vec2.hpp"

#include <vulkan/vulkan.h>

namespace crimson
{
    // What a graphics device needs from the window it draws to. The platform layer (platform/glfw/ today) implements
    // it, so the devices never call the windowing library.
    class WindowGraphics
    {
    public:
        virtual ~WindowGraphics() = default;

        // Vulkan: the instance extensions the window system needs, and the surface for the window
        [[nodiscard]] virtual std::vector<const char*> GetVulkanInstanceExtensions() const = 0;
        virtual VkResult CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const = 0;

        // OpenGL: the context of the window, its swap interval and buffer swap
        virtual void MakeContextCurrent() = 0;
        virtual void SetSwapInterval(int interval) = 0;
        virtual void SwapBuffers() = 0;

        // OpenGL: the size in pixels of the window's default framebuffer, which follows the window as it is resized
        [[nodiscard]] virtual glm::uvec2 GetFramebufferSize() const = 0;

        // Glad takes a C function pointer, so the interface hands one out rather than being one
        using GLLoader = void* (*)(const char* name);
        [[nodiscard]] virtual GLLoader GetGLLoader() const = 0;
    };
}
