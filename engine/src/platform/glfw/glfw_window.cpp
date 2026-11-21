#include <vulkan/vulkan.h>

#include "glfw_window.hpp"

#include <optional>
#include <stdexcept>
#include <utility>
#include "crimson/core/log.hpp"
#include "crimson/graphics/graphics_api.hpp"
#include "../../graphics/create_graphics_backend.hpp"
#include "glfw_input.hpp"

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

        // Some shapes are missing on some platforms, which GLFW reports as an error that isn't one for the engine
        const GLFWerrorfun errorCallback = glfwSetErrorCallback(nullptr);

        const auto createShape = [this](CursorShape shape, int standardCursor)
        {
            m_shapes[static_cast<std::size_t>(shape)] = glfwCreateStandardCursor(standardCursor);
        };

        createShape(CursorShape::Arrow,              GLFW_ARROW_CURSOR);
        createShape(CursorShape::TextInput,          GLFW_IBEAM_CURSOR);
        createShape(CursorShape::ResizeAll,          GLFW_RESIZE_ALL_CURSOR);
        createShape(CursorShape::ResizeVertical,     GLFW_RESIZE_NS_CURSOR);
        createShape(CursorShape::ResizeHorizontal,   GLFW_RESIZE_EW_CURSOR);
        createShape(CursorShape::ResizeDiagonalUp,   GLFW_RESIZE_NESW_CURSOR);
        createShape(CursorShape::ResizeDiagonalDown, GLFW_RESIZE_NWSE_CURSOR);
        createShape(CursorShape::Hand,               GLFW_POINTING_HAND_CURSOR);
        createShape(CursorShape::NotAllowed,         GLFW_NOT_ALLOWED_CURSOR);

        glfwSetErrorCallback(errorCallback);

		glfwSetWindowCloseCallback(m_handle, [](GLFWwindow* window) {
			auto* windowsWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			windowsWindow->m_data.EventCallbackFn(event);
		});

        glfwSetKeyCallback(m_handle, [](GLFWwindow* window, int glfwKey, int, int action, int mods) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            const std::optional<Key> key = ToKey(glfwKey);
            if (!key)
                return;

            const KeyModifiers modifiers = ToModifiers(mods);

            if (action == GLFW_RELEASE)
            {
                KeyReleaseEvent event(*key, modifiers);
                self->m_data.EventCallbackFn(event);
            }
            else
            {
                KeyPressEvent event(*key, modifiers, action == GLFW_REPEAT);
                self->m_data.EventCallbackFn(event);
            }
        });

        glfwSetCharCallback(m_handle, [](GLFWwindow* window, unsigned int codepoint) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            TextInputEvent event(static_cast<char32_t>(codepoint));
            self->m_data.EventCallbackFn(event);
        });

        glfwSetMouseButtonCallback(m_handle, [](GLFWwindow* window, int glfwButton, int action, int mods) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            const std::optional<MouseButton> button = ToMouseButton(glfwButton);
            if (!button)
                return;

            const KeyModifiers modifiers = ToModifiers(mods);

            if (action == GLFW_PRESS)
            {
                MouseButtonPressEvent event(*button, modifiers);
                self->m_data.EventCallbackFn(event);
            }
            else
            {
                MouseButtonReleaseEvent event(*button, modifiers);
                self->m_data.EventCallbackFn(event);
            }
        });

        glfwSetCursorPosCallback(m_handle, [](GLFWwindow* window, double x, double y) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            MouseMoveEvent event = self->m_cursor.Move(glm::vec2(static_cast<float>(x), static_cast<float>(y)));
            self->m_data.EventCallbackFn(event);
        });

        // Entering needs no event, as the first move shows where the cursor is
        glfwSetCursorEnterCallback(m_handle, [](GLFWwindow* window, int entered) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            if (!entered)
            {
                MouseLeaveEvent event;
                self->m_data.EventCallbackFn(event);
            }
        });

        glfwSetScrollCallback(m_handle, [](GLFWwindow* window, double x, double y) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            MouseScrollEvent event(glm::vec2(static_cast<float>(x), static_cast<float>(y)));
            self->m_data.EventCallbackFn(event);
        });

        glfwSetWindowFocusCallback(m_handle, [](GLFWwindow* window, int focused) {
            auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));

            WindowFocusEvent event(focused == GLFW_TRUE);
            self->m_data.EventCallbackFn(event);
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
            // Before the window, as a cursor can be in use
            for (GLFWcursor* shape : m_shapes)
                glfwDestroyCursor(shape);

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

    void GLFWWindow::SetCursorLocked(bool locked)
    {
        if (locked == m_cursor.IsLocked())
            return;

        if (locked)
        {
            double x = 0.0, y = 0.0;
            glfwGetCursorPos(m_handle, &x, &y);
            m_cursor.Lock(glm::vec2(static_cast<float>(x), static_cast<float>(y)));

            glfwSetInputMode(m_handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(m_handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        else
        {
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(m_handle, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

            glfwSetInputMode(m_handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

            // Back where the gesture started, which the mode change alone doesn't do on every platform
            const glm::vec2 lockedAt = m_cursor.Unlock();
            glfwSetCursorPos(m_handle, lockedAt.x, lockedAt.y);
        }
    }

    void GLFWWindow::SetCursorShape(CursorShape shape)
    {
        if (shape == m_shape || m_cursor.IsLocked())
            return;

        m_shape = shape;
        glfwSetCursor(m_handle, m_shapes[static_cast<std::size_t>(shape)]);
    }

    std::string GLFWWindow::GetClipboardText() const
    {
        const char* text = glfwGetClipboardString(m_handle);
        return text ? text : std::string();
    }

    void GLFWWindow::SetClipboardText(const std::string& text)
    {
        glfwSetClipboardString(m_handle, text.c_str());
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

    glm::uvec2 GLFWWindow::GetFramebufferSize() const
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_handle, &width, &height);
        return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    }

    WindowGraphics::GLLoader GLFWWindow::GetGLLoader() const
    {
        return reinterpret_cast<GLLoader>(glfwGetProcAddress);
    }
}
