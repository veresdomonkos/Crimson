#include "crimson_editor/ui/imgui_platform.hpp"

#include <algorithm>
#include <cfloat>

#include "imgui_convert.hpp"

namespace crimson::editor::ui
{
    // ImGui wants the modifiers as keys of their own before the key they change
    static void AddModifiers(ImGuiIO& io, KeyModifiers modifiers)
    {
        io.AddKeyEvent(ImGuiMod_Ctrl, HasModifier(modifiers, KeyModifiers::Control));
        io.AddKeyEvent(ImGuiMod_Shift, HasModifier(modifiers, KeyModifiers::Shift));
        io.AddKeyEvent(ImGuiMod_Alt, HasModifier(modifiers, KeyModifiers::Alt));
        io.AddKeyEvent(ImGuiMod_Super, HasModifier(modifiers, KeyModifiers::Super));
    }

    // The cursors ImGui has no shape for in the window's, as nothing in the editor hides the cursor
    static CursorShape ToCursorShape(ImGuiMouseCursor cursor)
    {
        switch (cursor)
        {
            case ImGuiMouseCursor_TextInput: return CursorShape::TextInput;
            case ImGuiMouseCursor_ResizeAll: return CursorShape::ResizeAll;
            case ImGuiMouseCursor_ResizeNS: return CursorShape::ResizeVertical;
            case ImGuiMouseCursor_ResizeEW: return CursorShape::ResizeHorizontal;
            case ImGuiMouseCursor_ResizeNESW: return CursorShape::ResizeDiagonalUp;
            case ImGuiMouseCursor_ResizeNWSE: return CursorShape::ResizeDiagonalDown;
            case ImGuiMouseCursor_Hand: return CursorShape::Hand;
            case ImGuiMouseCursor_NotAllowed: return CursorShape::NotAllowed;
            default: return CursorShape::Arrow;
        }
    }

    ImGuiPlatform::ImGuiPlatform(Window& window)
        : m_window(window)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = "crimson";
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        platformIO.Platform_GetClipboardTextFn = GetClipboardText;
        platformIO.Platform_SetClipboardTextFn = SetClipboardText;
        platformIO.Platform_ClipboardUserData = this;
    }

    ImGuiPlatform::~ImGuiPlatform()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = nullptr;
        io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;

        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        platformIO.Platform_GetClipboardTextFn = nullptr;
        platformIO.Platform_SetClipboardTextFn = nullptr;
        platformIO.Platform_ClipboardUserData = nullptr;
    }

    const char* ImGuiPlatform::GetClipboardText(ImGuiContext*)
    {
        auto* self = static_cast<ImGuiPlatform*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        self->m_clipboardText = self->m_window.GetClipboardText();
        return self->m_clipboardText.c_str();
    }

    void ImGuiPlatform::SetClipboardText(ImGuiContext*, const char* text)
    {
        static_cast<ImGuiPlatform*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->m_window.SetClipboardText(text);
    }

    void ImGuiPlatform::OnEvent(Event& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        EventDispatcher dispatcher(event);

        dispatcher.Dispatch<KeyPressEvent>([&io](KeyPressEvent& press)
        {
            AddModifiers(io, press.GetModifiers());
            io.AddKeyEvent(ToImGuiKey(press.GetKey()), true);
            return false;
        });

        dispatcher.Dispatch<KeyReleaseEvent>([&io](KeyReleaseEvent& release)
        {
            AddModifiers(io, release.GetModifiers());
            io.AddKeyEvent(ToImGuiKey(release.GetKey()), false);
            return false;
        });

        dispatcher.Dispatch<TextInputEvent>([&io](TextInputEvent& text)
        {
            io.AddInputCharacter(static_cast<unsigned int>(text.GetCodepoint()));
            return false;
        });

        dispatcher.Dispatch<MouseButtonPressEvent>([&io](MouseButtonPressEvent& press)
        {
            AddModifiers(io, press.GetModifiers());
            io.AddMouseButtonEvent(static_cast<int>(press.GetButton()), true);
            return false;
        });

        dispatcher.Dispatch<MouseButtonReleaseEvent>([&io](MouseButtonReleaseEvent& release)
        {
            AddModifiers(io, release.GetModifiers());
            io.AddMouseButtonEvent(static_cast<int>(release.GetButton()), false);
            return false;
        });

        dispatcher.Dispatch<MouseMoveEvent>([&io](MouseMoveEvent& move)
        {
            io.AddMousePosEvent(move.GetPosition().x, move.GetPosition().y);
            return false;
        });

        dispatcher.Dispatch<MouseLeaveEvent>([&io](MouseLeaveEvent&)
        {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            return false;
        });

        dispatcher.Dispatch<MouseScrollEvent>([&io](MouseScrollEvent& scroll)
        {
            io.AddMouseWheelEvent(scroll.GetOffset().x, scroll.GetOffset().y);
            return false;
        });

        dispatcher.Dispatch<WindowFocusEvent>([&io](WindowFocusEvent& focus)
        {
            io.AddFocusEvent(focus.IsFocused());
            return false;
        });
    }

    void ImGuiPlatform::NewFrame(float deltaTime)
    {
        ImGuiIO& io = ImGui::GetIO();

        // The window's coordinates are pixels, so there is no scale between the two. How big things are drawn is the theme's.
        io.DisplaySize = ImVec2(static_cast<float>(m_window.Width()), static_cast<float>(m_window.Height()));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::max(deltaTime, 1e-4f);

        if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange))
            m_window.SetCursorShape(ToCursorShape(ImGui::GetMouseCursor()));
    }
}
