#pragma once
#include <functional>

#include <crimson/core/input.hpp>
#include <crimson/core/window.hpp>
#include <crimson/graphics/graphics_api.hpp>
#include <crimson/graphics/graphics_backend.hpp>
#include <crimson/project/project.hpp>
#include <crimson/renderer/frame_recorder.hpp>

namespace crimson::editor
{
    class EditorIcons;
    class EditorSettings;
    class RecentProjects;

    // What the application owns for its whole run, and every mode can use
    struct EditorServices
    {
        Window& AppWindow;
        GraphicsBackend& Graphics;
        Input& AppInput;
        GraphicsApi RendererType;
        EditorIcons& Icons;
        RecentProjects& Recents;
        EditorSettings& Settings;

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

        // Fills in the frame: its lighting and passes. The UI pass goes after them.
        virtual void Record(FrameRecorder& frame) {}

        // Builds the UI, after Record, while the frame is still being recorded
        virtual void OnImGui() = 0;

        // After the frame
        virtual void Update(float deltaTime) {}

        // Something wants to close the mode. A mode with unsaved work can ask first, and calls proceed if it goes ahead.
        virtual void RequestClose(std::function<void()> proceed) { proceed(); }
    };
}
