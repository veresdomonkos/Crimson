#include "opengl_backend.hpp"

#include <glad/glad.h>
#include "crimson/core/log.hpp"

namespace crimson::opengl
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
                    const auto& cmd = commandView.As<ClearCommand>();
                    glClearColor(cmd.Color.r, cmd.Color.g, cmd.Color.b, cmd.Color.a);
                    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
                    break;
                }
                case RendererCommandType::DrawIndexed:
                {
                    const auto& cmd = commandView.As<DrawIndexedCommand>();
                    //glUseProgram(static_cast<GLuint>(cmd.ShaderHandle));
                    glBindVertexArray(static_cast<GLuint>(cmd.VertexBufferHandle));
                    glVertexArrayElementBuffer(static_cast<GLuint>(cmd.VertexBufferHandle), static_cast<GLuint>(cmd.IndexBufferHandle));
                    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.IndexCount), GL_UNSIGNED_INT, nullptr);
                    break;
                }
                default:
                    break;
            }
        }
    }
}
