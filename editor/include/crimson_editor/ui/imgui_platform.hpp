#pragma once
#include <string>

#include <crimson/core/events.hpp>
#include <crimson/core/window.hpp>
#include <imgui.h>

namespace crimson::editor::ui
{
    // Feeds ImGui from the window: its events become ImGui's input events, and ImGui's mouse cursor and clipboard go
    // back to the window. It needs an ImGui context for as long as it lives, and the window has to outlive it.
    class ImGuiPlatform
    {
    public:
        explicit ImGuiPlatform(Window& window);
        ~ImGuiPlatform();

        ImGuiPlatform(const ImGuiPlatform&) = delete;
        ImGuiPlatform& operator=(const ImGuiPlatform&) = delete;

        // Doesn't mark the event handled, as the rest of the editor wants the same events
        void OnEvent(Event& event);

        // Before ImGui::NewFrame(): the display's size, the time since the last frame, and the cursor ImGui wants
        void NewFrame(float deltaTime);

    private:
        // What the clipboard functions of ImGui hand out has to live until the next call
        static const char* GetClipboardText(ImGuiContext* context);
        static void SetClipboardText(ImGuiContext* context, const char* text);

        Window& m_window;
        std::string m_clipboardText;
    };
}
