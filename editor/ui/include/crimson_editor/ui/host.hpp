#pragma once
#include <filesystem>
#include <memory>

#include <crimson/core/events.hpp>
#include <crimson/core/window.hpp>
#include <crimson/renderer/frame_recorder.hpp>
#include <crimson/renderer/resource_manager.hpp>

namespace crimson::editor::ui
{
    // The UI's life in the editor: it owns what draws and feeds the UI, so nothing else has to know what that is. It
    // needs the window and the GPU to outlive it.
    class Host
    {
    public:
        // The theme is applied at the window's content scale, and the layout is kept in the file
        Host(Window& window, GpuResourceManager& gpu, const std::filesystem::path& layoutFile);
        ~Host();

        Host(const Host&) = delete;
        Host& operator=(const Host&) = delete;

        // Doesn't mark the event handled, as the rest of the editor wants the same events
        void OnEvent(Event& event);

        // Starts the frame the UI is drawn in
        void BeginFrame(float deltaTime);

        // Ends it, and records what was drawn as a pass of the frame, over what is there
        void EndFrame(FrameRecorder& frame);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
