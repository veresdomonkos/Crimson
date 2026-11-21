#include "imgui_convert.hpp"

namespace crimson::editor::ui
{
    ImGuiKey ToImGuiKey(Key key)
    {
        switch (key)
        {
            case Key::A: return ImGuiKey_A;
            case Key::B: return ImGuiKey_B;
            case Key::C: return ImGuiKey_C;
            case Key::D: return ImGuiKey_D;
            case Key::E: return ImGuiKey_E;
            case Key::F: return ImGuiKey_F;
            case Key::G: return ImGuiKey_G;
            case Key::H: return ImGuiKey_H;
            case Key::I: return ImGuiKey_I;
            case Key::J: return ImGuiKey_J;
            case Key::K: return ImGuiKey_K;
            case Key::L: return ImGuiKey_L;
            case Key::M: return ImGuiKey_M;
            case Key::N: return ImGuiKey_N;
            case Key::O: return ImGuiKey_O;
            case Key::P: return ImGuiKey_P;
            case Key::Q: return ImGuiKey_Q;
            case Key::R: return ImGuiKey_R;
            case Key::S: return ImGuiKey_S;
            case Key::T: return ImGuiKey_T;
            case Key::U: return ImGuiKey_U;
            case Key::V: return ImGuiKey_V;
            case Key::W: return ImGuiKey_W;
            case Key::X: return ImGuiKey_X;
            case Key::Y: return ImGuiKey_Y;
            case Key::Z: return ImGuiKey_Z;

            case Key::D0: return ImGuiKey_0;
            case Key::D1: return ImGuiKey_1;
            case Key::D2: return ImGuiKey_2;
            case Key::D3: return ImGuiKey_3;
            case Key::D4: return ImGuiKey_4;
            case Key::D5: return ImGuiKey_5;
            case Key::D6: return ImGuiKey_6;
            case Key::D7: return ImGuiKey_7;
            case Key::D8: return ImGuiKey_8;
            case Key::D9: return ImGuiKey_9;

            case Key::F1: return ImGuiKey_F1;
            case Key::F2: return ImGuiKey_F2;
            case Key::F3: return ImGuiKey_F3;
            case Key::F4: return ImGuiKey_F4;
            case Key::F5: return ImGuiKey_F5;
            case Key::F6: return ImGuiKey_F6;
            case Key::F7: return ImGuiKey_F7;
            case Key::F8: return ImGuiKey_F8;
            case Key::F9: return ImGuiKey_F9;
            case Key::F10: return ImGuiKey_F10;
            case Key::F11: return ImGuiKey_F11;
            case Key::F12: return ImGuiKey_F12;
            case Key::F13: return ImGuiKey_F13;
            case Key::F14: return ImGuiKey_F14;
            case Key::F15: return ImGuiKey_F15;
            case Key::F16: return ImGuiKey_F16;
            case Key::F17: return ImGuiKey_F17;
            case Key::F18: return ImGuiKey_F18;
            case Key::F19: return ImGuiKey_F19;
            case Key::F20: return ImGuiKey_F20;
            case Key::F21: return ImGuiKey_F21;
            case Key::F22: return ImGuiKey_F22;
            case Key::F23: return ImGuiKey_F23;
            case Key::F24: return ImGuiKey_F24;

            case Key::Space: return ImGuiKey_Space;
            case Key::Apostrophe: return ImGuiKey_Apostrophe;
            case Key::Comma: return ImGuiKey_Comma;
            case Key::Minus: return ImGuiKey_Minus;
            case Key::Period: return ImGuiKey_Period;
            case Key::Slash: return ImGuiKey_Slash;
            case Key::Semicolon: return ImGuiKey_Semicolon;
            case Key::Equal: return ImGuiKey_Equal;
            case Key::LeftBracket: return ImGuiKey_LeftBracket;
            case Key::Backslash: return ImGuiKey_Backslash;
            case Key::RightBracket: return ImGuiKey_RightBracket;
            case Key::GraveAccent: return ImGuiKey_GraveAccent;

            case Key::Escape: return ImGuiKey_Escape;
            case Key::Enter: return ImGuiKey_Enter;
            case Key::Tab: return ImGuiKey_Tab;
            case Key::Backspace: return ImGuiKey_Backspace;
            case Key::Insert: return ImGuiKey_Insert;
            case Key::Delete: return ImGuiKey_Delete;

            case Key::Right: return ImGuiKey_RightArrow;
            case Key::Left: return ImGuiKey_LeftArrow;
            case Key::Down: return ImGuiKey_DownArrow;
            case Key::Up: return ImGuiKey_UpArrow;
            case Key::PageUp: return ImGuiKey_PageUp;
            case Key::PageDown: return ImGuiKey_PageDown;
            case Key::Home: return ImGuiKey_Home;
            case Key::End: return ImGuiKey_End;

            case Key::CapsLock: return ImGuiKey_CapsLock;
            case Key::ScrollLock: return ImGuiKey_ScrollLock;
            case Key::NumLock: return ImGuiKey_NumLock;
            case Key::PrintScreen: return ImGuiKey_PrintScreen;
            case Key::Pause: return ImGuiKey_Pause;

            case Key::Keypad0: return ImGuiKey_Keypad0;
            case Key::Keypad1: return ImGuiKey_Keypad1;
            case Key::Keypad2: return ImGuiKey_Keypad2;
            case Key::Keypad3: return ImGuiKey_Keypad3;
            case Key::Keypad4: return ImGuiKey_Keypad4;
            case Key::Keypad5: return ImGuiKey_Keypad5;
            case Key::Keypad6: return ImGuiKey_Keypad6;
            case Key::Keypad7: return ImGuiKey_Keypad7;
            case Key::Keypad8: return ImGuiKey_Keypad8;
            case Key::Keypad9: return ImGuiKey_Keypad9;
            case Key::KeypadDecimal: return ImGuiKey_KeypadDecimal;
            case Key::KeypadDivide: return ImGuiKey_KeypadDivide;
            case Key::KeypadMultiply: return ImGuiKey_KeypadMultiply;
            case Key::KeypadSubtract: return ImGuiKey_KeypadSubtract;
            case Key::KeypadAdd: return ImGuiKey_KeypadAdd;
            case Key::KeypadEnter: return ImGuiKey_KeypadEnter;
            case Key::KeypadEqual: return ImGuiKey_KeypadEqual;

            case Key::LeftShift: return ImGuiKey_LeftShift;
            case Key::LeftControl: return ImGuiKey_LeftCtrl;
            case Key::LeftAlt: return ImGuiKey_LeftAlt;
            case Key::LeftSuper: return ImGuiKey_LeftSuper;
            case Key::RightShift: return ImGuiKey_RightShift;
            case Key::RightControl: return ImGuiKey_RightCtrl;
            case Key::RightAlt: return ImGuiKey_RightAlt;
            case Key::RightSuper: return ImGuiKey_RightSuper;
            case Key::Menu: return ImGuiKey_Menu;
        }

        return ImGuiKey_None;
    }
}
