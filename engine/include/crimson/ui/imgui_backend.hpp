#pragma once
#include <memory>

#include <imgui/imgui.h>
#include "crimson/core/window.hpp"
#include "crimson/renderer/renderer.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    class ImGuiBackend
    {
    public:
        virtual ~ImGuiBackend() = default;
        virtual void NewFrame() = 0;
        virtual void RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles) = 0;
        virtual ImTextureID GetOrCreateTextureId(TextureHandle texture) = 0;
    };
}
