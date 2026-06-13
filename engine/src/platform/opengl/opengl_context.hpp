#pragma once
#include "crimson/renderer/rendering_context.hpp"

namespace crimson
{
    class OpenGLContext : public RenderingContext
    {
    public:
        OpenGLContext(void* windowHandle);
        void SwapBuffers() override;
        void MakeCurrent() override;
    private:
        void* m_windowHandle;
    };
}
