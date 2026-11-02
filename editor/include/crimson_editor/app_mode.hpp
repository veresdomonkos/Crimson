#pragma once
#include <functional>

#include <crimson/core/input.hpp>
#include <crimson/core/window.hpp>
#include <crimson/graphics/graphincs_backend.hpp>
#include <crimson/project/project.hpp>
#include <crimson/renderer/frame_context.hpp>
#include <crimson/renderer/renderer_api.hpp>

namespace crimson::editor
{
    class EditorIcons;
    class RecentProjects;

    // What the application owns for its whole run, and every mode can use
    struct EditorServices
    {
        Window& AppWindow;
        GraphicsBackend& Graphics;
        Input& AppInput;
        RendererAPIType RendererType;
        EditorIcons& Icons;
        RecentProjects& Recents;

        // These only make a request, the mode changes after the current frame
        std::function<void(Project)> OpenProject;
        std::function<void()> CloseProject;
    };

    // What the application is showing. Exactly one is active, and they are only swapped between frames,
    // so a mode can free its GPU resources when it is destroyed.
    class AppMode
    {
    public:
        virtual ~AppMode() = default;

        // Before the frame starts
        virtual void PrepareFrame() {}
        [[nodiscard]] virtual const FrameLightingData& Lighting() const { return m_noLighting; }

        // Inside the frame
        virtual void RecordPasses(FrameContext& frame) {}
        virtual void OnImGui() = 0;

        // After the frame
        virtual void Update(float deltaTime) {}
    private:
        FrameLightingData m_noLighting;
    };
}
