#include "crimson_editor/ui/input.hpp"

#include <string>

#include <imgui.h>

#include "imgui_convert.hpp"

namespace crimson::editor::ui
{
    namespace input
    {
        bool KeyPressed(Key key, bool repeat) { return ImGui::IsKeyPressed(ToImGuiKey(key), repeat); }

        bool MouseClicked(MouseButton button) { return ImGui::IsMouseClicked(ToImGuiMouseButton(button)); }
        bool MouseDoubleClicked(MouseButton button) { return ImGui::IsMouseDoubleClicked(ToImGuiMouseButton(button)); }
        bool MouseDown(MouseButton button) { return ImGui::IsMouseDown(ToImGuiMouseButton(button)); }
        glm::vec2 MousePosition() { return FromImVec2(ImGui::GetIO().MousePos); }
        float MouseWheel() { return ImGui::GetIO().MouseWheel; }

        KeyModifiers Modifiers()
        {
            const ImGuiIO& io = ImGui::GetIO();

            KeyModifiers modifiers = KeyModifiers::None;
            if (io.KeyShift) modifiers = modifiers | KeyModifiers::Shift;
            if (io.KeyCtrl) modifiers = modifiers | KeyModifiers::Control;
            if (io.KeyAlt) modifiers = modifiers | KeyModifiers::Alt;
            if (io.KeySuper) modifiers = modifiers | KeyModifiers::Super;
            return modifiers;
        }

        bool IsEditingItem() { return ImGui::IsAnyItemActive(); }
        bool IsAnyItemHovered() { return ImGui::IsAnyItemHovered(); }

        double Time() { return ImGui::GetTime(); }
        float DeltaTime() { return ImGui::GetIO().DeltaTime; }

        void SetClipboard(std::string_view text) { ImGui::SetClipboardText(std::string(text).c_str()); }
    }

    namespace item
    {
        bool Hovered(bool whenDisabled) { return ImGui::IsItemHovered(whenDisabled ? ImGuiHoveredFlags_AllowWhenDisabled : ImGuiHoveredFlags_None); }
        bool Clicked(MouseButton button) { return ImGui::IsItemClicked(ToImGuiMouseButton(button)); }
        bool DoubleClicked() { return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left); }
        bool Active() { return ImGui::IsItemActive(); }
        bool Deactivated() { return ImGui::IsItemDeactivated(); }
        bool DeactivatedAfterEdit() { return ImGui::IsItemDeactivatedAfterEdit(); }
        bool ToggledOpen() { return ImGui::IsItemToggledOpen(); }
        bool Visible() { return ImGui::IsItemVisible(); }
        Rect Bounds() { return Rect{ FromImVec2(ImGui::GetItemRectMin()), FromImVec2(ImGui::GetItemRectMax()) }; }
    }
}
