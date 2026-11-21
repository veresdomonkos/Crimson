#include <vulkan/vulkan.h>

#include "glfw_window.hpp"

#include <stdexcept>
#include <utility>
#include "crimson/core/log.hpp"
#include "crimson/graphics/graphics_api.hpp"
#include "../../graphics/create_graphics_backend.hpp"

// Last, as X11's headers define macros such as None
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
// CI builds GLFW without Wayland
#if __has_include(<wayland-client.h>)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#endif
#include <GLFW/glfw3native.h>

namespace crimson
{
	Unique<Window> Window::Create(GraphicsApi rendererAPI, WindowData data)
	{
		glfwSetErrorCallback([](int, const char* description) { LOG_ERROR("[GLFW] {}", description); });

		// By default GLFW loads a DLL by a fixed name on its own, which can miss the loader the engine links or find a different one
		glfwInitVulkanLoader(vkGetInstanceProcAddr);

		// A window's destructor terminates GLFW, which also forgets the hints, so each window starts over
		if (!glfwInit())
		{
			LOG_ERROR("Failed to initialize GLFW!");
			return nullptr;
		}

		switch (rendererAPI)
		{
			case GraphicsApi::OpenGL:
				glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
				break;

			case GraphicsApi::Vulkan:
				glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
				break;

			default:
				break;
		}

        return CreateUnique<glfw::GLFWWindow>(rendererAPI, std::move(data));
	}
}

namespace crimson::glfw
{
    GLFWWindow::GLFWWindow(GraphicsApi api, WindowData data) : m_api(api), m_data(std::move(data)), m_handle(nullptr)
	{
		m_handle = glfwCreateWindow(static_cast<int>(m_data.Width), static_cast<int>(m_data.Height), m_data.Title.c_str(), nullptr, nullptr);

		if (!m_handle)
		{
			LOG_ERROR("Failed to create GLFW Window!");
			glfwTerminate();
			return;
		}

		glfwSetWindowUserPointer(m_handle, this);

		glfwSetWindowCloseCallback(m_handle, [](GLFWwindow* window) {
			auto* windowsWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			windowsWindow->m_data.EventCallbackFn(event);
		});

	    glfwSetKeyCallback(m_handle, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
	        auto* windowsWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            switch (action)
            {
                case GLFW_PRESS:
                {
                    KeyPressEvent event(static_cast<KeyCode>(key), false);
                    windowsWindow->m_data.EventCallbackFn(event);
                    break;
                }
                case GLFW_REPEAT:
                {
                    KeyPressEvent event(static_cast<KeyCode>(key), true);
                    windowsWindow->m_data.EventCallbackFn(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    break;
                }
            }
        });

	    glfwSetWindowSizeCallback(m_handle, [](GLFWwindow* window, int width, int height) {
	        auto* windowsWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
	        windowsWindow->m_data.Width = static_cast<uint32_t>(width);
	        windowsWindow->m_data.Height = static_cast<uint32_t>(height);

	        WindowResizeEvent event(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	        windowsWindow->m_data.EventCallbackFn(event);
	    });
	}

    GLFWWindow::~GLFWWindow()
    {
	    if (m_handle)
	    {
	        glfwDestroyWindow(m_handle);
	    }

	    glfwTerminate();
    }

    void GLFWWindow::PollEvents()
	{
		glfwPollEvents();
	}

    void GLFWWindow::SetTitle(const std::string& title)
    {
        m_data.Title = title;
        glfwSetWindowTitle(m_handle, title.c_str());
    }

    void GLFWWindow::SetSize(uint32_t width, uint32_t height)
    {
        glfwSetWindowSize(m_handle, static_cast<int>(width), static_cast<int>(height));
    }

    OSWindowHandle GLFWWindow::GetOSHandle() const
    {
#if defined(_WIN32)
        return { OSWindowHandle::Kind::Win32, reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(m_handle)) };
#elif defined(__APPLE__)
        return { OSWindowHandle::Kind::Cocoa, reinterpret_cast<std::uintptr_t>(glfwGetCocoaWindow(m_handle)) };
#else
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            return { OSWindowHandle::Kind::Wayland, reinterpret_cast<std::uintptr_t>(glfwGetWaylandWindow(m_handle)) };
#endif
        return { OSWindowHandle::Kind::X11, static_cast<std::uintptr_t>(glfwGetX11Window(m_handle)) };
#endif
    }

    std::unique_ptr<GraphicsBackend> GLFWWindow::CreateGraphicsBackend()
    {
        return crimson::CreateGraphicsBackend(m_api, *this, *this);
    }

    std::vector<const char*> GLFWWindow::GetVulkanInstanceExtensions() const
    {
        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);

        if (!extensions)
            throw std::runtime_error("Failed to get GLFW Vulkan extensions.");

        return std::vector<const char*>(extensions, extensions + count);
    }

    VkResult GLFWWindow::CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const
    {
        return glfwCreateWindowSurface(instance, m_handle, nullptr, surface);
    }

    WindowGraphics::GLLoader GLFWWindow::GetGLLoader() const
    {
        return reinterpret_cast<GLLoader>(glfwGetProcAddress);
    }
}
