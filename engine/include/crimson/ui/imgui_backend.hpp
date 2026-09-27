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
        static std::unique_ptr<ImGuiBackend> Create();

        virtual ~ImGuiBackend() = default;
        virtual void Init(const Renderer& handles, const Window& window) = 0;
        virtual void NewFrame() = 0;
        virtual void RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles) = 0;
        virtual void Shutdown() = 0;
        virtual ImTextureID GetOrCreateTextureId(ResourceManager& resourceManager, TextureHandle texture) = 0;
    };
}
