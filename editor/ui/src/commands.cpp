#include "crimson_editor/ui/commands.hpp"

namespace crimson::editor::ui
{
    static bool IsBetween(Key key, Key first, Key last)
    {
        return key >= first && key <= last;
    }

    static std::string KeyName(Key key)
    {
        const int index = static_cast<int>(key);

        if (IsBetween(key, Key::A, Key::Z))
            return std::string(1, static_cast<char>('A' + index - static_cast<int>(Key::A)));

        if (IsBetween(key, Key::D0, Key::D9))
            return std::string(1, static_cast<char>('0' + index - static_cast<int>(Key::D0)));

        if (IsBetween(key, Key::F1, Key::F24))
            return "F" + std::to_string(index - static_cast<int>(Key::F1) + 1);

        if (IsBetween(key, Key::Keypad0, Key::Keypad9))
            return "Keypad " + std::to_string(index - static_cast<int>(Key::Keypad0));

        switch (key)
        {
            case Key::Space: return "Space";
            case Key::Apostrophe: return "'";
            case Key::Comma: return ",";
            case Key::Minus: return "-";
            case Key::Period: return ".";
            case Key::Slash: return "/";
            case Key::Semicolon: return ";";
            case Key::Equal: return "=";
            case Key::LeftBracket: return "[";
            case Key::Backslash: return "\\";
            case Key::RightBracket: return "]";
            case Key::GraveAccent: return "`";

            case Key::Escape: return "Esc";
            case Key::Enter: return "Enter";
            case Key::Tab: return "Tab";
            case Key::Backspace: return "Backspace";
            case Key::Insert: return "Insert";
            case Key::Delete: return "Delete";

            case Key::Right: return "Right";
            case Key::Left: return "Left";
            case Key::Down: return "Down";
            case Key::Up: return "Up";
            case Key::PageUp: return "Page Up";
            case Key::PageDown: return "Page Down";
            case Key::Home: return "Home";
            case Key::End: return "End";

            case Key::CapsLock: return "Caps Lock";
            case Key::ScrollLock: return "Scroll Lock";
            case Key::NumLock: return "Num Lock";
            case Key::PrintScreen: return "Print Screen";
            case Key::Pause: return "Pause";

            case Key::KeypadDecimal: return "Keypad .";
            case Key::KeypadDivide: return "Keypad /";
            case Key::KeypadMultiply: return "Keypad *";
            case Key::KeypadSubtract: return "Keypad -";
            case Key::KeypadAdd: return "Keypad +";
            case Key::KeypadEnter: return "Keypad Enter";
            case Key::KeypadEqual: return "Keypad =";

            case Key::LeftShift:
            case Key::RightShift: return "Shift";
            case Key::LeftControl:
            case Key::RightControl: return "Ctrl";
            case Key::LeftAlt:
            case Key::RightAlt: return "Alt";
            case Key::LeftSuper:
            case Key::RightSuper: return "Super";
            case Key::Menu: return "Menu";

            default: return {};
        }
    }

    std::string ToText(const Shortcut& shortcut)
    {
        std::string text;

        if (HasModifier(shortcut.Modifiers, KeyModifiers::Control))
            text += "Ctrl+";
        if (HasModifier(shortcut.Modifiers, KeyModifiers::Alt))
            text += "Alt+";
        if (HasModifier(shortcut.Modifiers, KeyModifiers::Shift))
            text += "Shift+";
        if (HasModifier(shortcut.Modifiers, KeyModifiers::Super))
            text += "Super+";

        return text + KeyName(shortcut.Key);
    }
}
