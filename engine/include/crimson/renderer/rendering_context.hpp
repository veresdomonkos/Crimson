#pragma once
#include <memory>

namespace crimson
{
    class RenderingContext
    {
    public:
        virtual ~RenderingContext() = default;
        virtual void SwapBuffers() = 0;
        virtual void MakeCurrent() = 0;

        static std::unique_ptr<RenderingContext> Create(void* windowHandle);
    };
}
