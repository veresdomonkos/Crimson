#include "opengl_backend.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "crimson/core/log.hpp"

namespace crimson
{
    static bool s_gladInitialized = false;

    void OpenGLBackend::Initialize(void* windowHandle)
    {
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(windowHandle));

        if (!s_gladInitialized)
        {
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            {
                LOG_ERROR("[Renderer] Failed to initialize GLAD!");
                return;
            }

            LOG_WARN("[Renderer] GLAD Initialized successfully!");
            LOG_INFO("[Renderer] OpenGL Version: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
            LOG_INFO("[Renderer] GPU Vendor:     {}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
            LOG_INFO("[Renderer] Renderer:       {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        }

        s_gladInitialized = true;
    }

    void OpenGLBackend::Execute(RenderCommandQueue& commandQueue, void* windowHandle)
    {
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(windowHandle));

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const std::byte* ptr = commandQueue.GetBufferStart();
        const std::byte* endPtr = commandQueue.GetDataEnd();

        while (ptr < endPtr)
        {
            auto* header = reinterpret_cast<const RenderCommandHeader*>(ptr);
            switch (header->Type)
            {
                case RendererCommandType::Clear:
                {
                    auto *command = reinterpret_cast<const ClearCommand*>(header + 1);
                    glClearColor(command->Color.r, command->Color.g, command->Color.b, command->Color.a);
                    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
                    break;
                }
                default:
                    break;
            }

            ptr += sizeof(RenderCommandHeader) + header->Size;
        }

        commandQueue.Clear();
    }

    void OpenGLBackend::Present(void* windowHandle)
    {
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(windowHandle));
        glfwSwapBuffers(static_cast<GLFWwindow*>(windowHandle));
    }
}
