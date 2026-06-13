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
    }
}
