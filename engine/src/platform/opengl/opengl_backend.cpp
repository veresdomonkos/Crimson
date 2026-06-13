#include "opengl_backend.hpp"

#include <glad/glad.h>
#include "crimson/core/log.hpp"

namespace crimson
{
    OpenGLBackend::OpenGLBackend()
    {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void OpenGLBackend::Execute(const RenderCommandQueue& commandQueue)
    {
        for (auto& commandView : commandQueue)
        {
            switch (commandView.GetType())
            {
                case RendererCommandType::Clear:
                {
                    const auto& command = commandView.As<ClearCommand>();
                    glClearColor(command.Color.r, command.Color.g, command.Color.b, command.Color.a);
                    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
                    break;
                }
                default:
                    break;
            }
        }
    }
}
