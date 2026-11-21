#include "glfw_input.hpp"

#include <array>
#include <iterator>

#include <GLFW/glfw3.h>

namespace crimson::glfw
{
    struct KeyMapping
    {
        int GlfwKey;
        Key EngineKey;
    };

    // Every Key once, which the static_asserts below check
    static constexpr KeyMapping kKeyTable[] =
    {
        { GLFW_KEY_A, Key::A }, { GLFW_KEY_B, Key::B }, { GLFW_KEY_C, Key::C }, { GLFW_KEY_D, Key::D },
        { GLFW_KEY_E, Key::E }, { GLFW_KEY_F, Key::F }, { GLFW_KEY_G, Key::G }, { GLFW_KEY_H, Key::H },
        { GLFW_KEY_I, Key::I }, { GLFW_KEY_J, Key::J }, { GLFW_KEY_K, Key::K }, { GLFW_KEY_L, Key::L },
        { GLFW_KEY_M, Key::M }, { GLFW_KEY_N, Key::N }, { GLFW_KEY_O, Key::O }, { GLFW_KEY_P, Key::P },
        { GLFW_KEY_Q, Key::Q }, { GLFW_KEY_R, Key::R }, { GLFW_KEY_S, Key::S }, { GLFW_KEY_T, Key::T },
        { GLFW_KEY_U, Key::U }, { GLFW_KEY_V, Key::V }, { GLFW_KEY_W, Key::W }, { GLFW_KEY_X, Key::X },
        { GLFW_KEY_Y, Key::Y }, { GLFW_KEY_Z, Key::Z },

        { GLFW_KEY_0, Key::D0 }, { GLFW_KEY_1, Key::D1 }, { GLFW_KEY_2, Key::D2 }, { GLFW_KEY_3, Key::D3 },
        { GLFW_KEY_4, Key::D4 }, { GLFW_KEY_5, Key::D5 }, { GLFW_KEY_6, Key::D6 }, { GLFW_KEY_7, Key::D7 },
        { GLFW_KEY_8, Key::D8 }, { GLFW_KEY_9, Key::D9 },

        { GLFW_KEY_F1, Key::F1 }, { GLFW_KEY_F2, Key::F2 }, { GLFW_KEY_F3, Key::F3 }, { GLFW_KEY_F4, Key::F4 },
        { GLFW_KEY_F5, Key::F5 }, { GLFW_KEY_F6, Key::F6 }, { GLFW_KEY_F7, Key::F7 }, { GLFW_KEY_F8, Key::F8 },
        { GLFW_KEY_F9, Key::F9 }, { GLFW_KEY_F10, Key::F10 }, { GLFW_KEY_F11, Key::F11 }, { GLFW_KEY_F12, Key::F12 },
        { GLFW_KEY_F13, Key::F13 }, { GLFW_KEY_F14, Key::F14 }, { GLFW_KEY_F15, Key::F15 }, { GLFW_KEY_F16, Key::F16 },
        { GLFW_KEY_F17, Key::F17 }, { GLFW_KEY_F18, Key::F18 }, { GLFW_KEY_F19, Key::F19 }, { GLFW_KEY_F20, Key::F20 },
        { GLFW_KEY_F21, Key::F21 }, { GLFW_KEY_F22, Key::F22 }, { GLFW_KEY_F23, Key::F23 }, { GLFW_KEY_F24, Key::F24 },

        { GLFW_KEY_SPACE, Key::Space }, { GLFW_KEY_APOSTROPHE, Key::Apostrophe }, { GLFW_KEY_COMMA, Key::Comma },
        { GLFW_KEY_MINUS, Key::Minus }, { GLFW_KEY_PERIOD, Key::Period }, { GLFW_KEY_SLASH, Key::Slash },
        { GLFW_KEY_SEMICOLON, Key::Semicolon }, { GLFW_KEY_EQUAL, Key::Equal },
        { GLFW_KEY_LEFT_BRACKET, Key::LeftBracket }, { GLFW_KEY_BACKSLASH, Key::Backslash },
        { GLFW_KEY_RIGHT_BRACKET, Key::RightBracket }, { GLFW_KEY_GRAVE_ACCENT, Key::GraveAccent },

        { GLFW_KEY_ESCAPE, Key::Escape }, { GLFW_KEY_ENTER, Key::Enter }, { GLFW_KEY_TAB, Key::Tab },
        { GLFW_KEY_BACKSPACE, Key::Backspace }, { GLFW_KEY_INSERT, Key::Insert }, { GLFW_KEY_DELETE, Key::Delete },
        { GLFW_KEY_RIGHT, Key::Right }, { GLFW_KEY_LEFT, Key::Left }, { GLFW_KEY_DOWN, Key::Down }, { GLFW_KEY_UP, Key::Up },
        { GLFW_KEY_PAGE_UP, Key::PageUp }, { GLFW_KEY_PAGE_DOWN, Key::PageDown },
        { GLFW_KEY_HOME, Key::Home }, { GLFW_KEY_END, Key::End },
        { GLFW_KEY_CAPS_LOCK, Key::CapsLock }, { GLFW_KEY_SCROLL_LOCK, Key::ScrollLock },
        { GLFW_KEY_NUM_LOCK, Key::NumLock }, { GLFW_KEY_PRINT_SCREEN, Key::PrintScreen }, { GLFW_KEY_PAUSE, Key::Pause },

        { GLFW_KEY_KP_0, Key::Keypad0 }, { GLFW_KEY_KP_1, Key::Keypad1 }, { GLFW_KEY_KP_2, Key::Keypad2 },
        { GLFW_KEY_KP_3, Key::Keypad3 }, { GLFW_KEY_KP_4, Key::Keypad4 }, { GLFW_KEY_KP_5, Key::Keypad5 },
        { GLFW_KEY_KP_6, Key::Keypad6 }, { GLFW_KEY_KP_7, Key::Keypad7 }, { GLFW_KEY_KP_8, Key::Keypad8 },
        { GLFW_KEY_KP_9, Key::Keypad9 },
        { GLFW_KEY_KP_DECIMAL, Key::KeypadDecimal }, { GLFW_KEY_KP_DIVIDE, Key::KeypadDivide },
        { GLFW_KEY_KP_MULTIPLY, Key::KeypadMultiply }, { GLFW_KEY_KP_SUBTRACT, Key::KeypadSubtract },
        { GLFW_KEY_KP_ADD, Key::KeypadAdd }, { GLFW_KEY_KP_ENTER, Key::KeypadEnter }, { GLFW_KEY_KP_EQUAL, Key::KeypadEqual },

        { GLFW_KEY_LEFT_SHIFT, Key::LeftShift }, { GLFW_KEY_LEFT_CONTROL, Key::LeftControl },
        { GLFW_KEY_LEFT_ALT, Key::LeftAlt }, { GLFW_KEY_LEFT_SUPER, Key::LeftSuper },
        { GLFW_KEY_RIGHT_SHIFT, Key::RightShift }, { GLFW_KEY_RIGHT_CONTROL, Key::RightControl },
        { GLFW_KEY_RIGHT_ALT, Key::RightAlt }, { GLFW_KEY_RIGHT_SUPER, Key::RightSuper },
        { GLFW_KEY_MENU, Key::Menu },
    };

    static constexpr bool EveryKeyIsMappedOnce()
    {
        std::array<int, kKeyCount> counts{};

        for (const KeyMapping& mapping : kKeyTable)
            ++counts[static_cast<std::size_t>(mapping.EngineKey)];

        for (const int count : counts)
        {
            if (count != 1)
                return false;
        }

        return true;
    }

    // ToKey returns the first match, so a GLFW key that two Keys share leaves the second one unreachable
    static constexpr bool EveryGlfwKeyIsMappedOnce()
    {
        for (std::size_t i = 0; i < std::size(kKeyTable); ++i)
        {
            for (std::size_t j = i + 1; j < std::size(kKeyTable); ++j)
            {
                if (kKeyTable[i].GlfwKey == kKeyTable[j].GlfwKey)
                    return false;
            }
        }

        return true;
    }

    static_assert(std::size(kKeyTable) == kKeyCount, "The table has to cover every Key");
    static_assert(EveryKeyIsMappedOnce(), "Every Key has to appear in the table exactly once");
    static_assert(EveryGlfwKeyIsMappedOnce(), "Every GLFW key can only be mapped to one Key");

    std::optional<Key> ToKey(int glfwKey)
    {
        for (const KeyMapping& mapping : kKeyTable)
        {
            if (mapping.GlfwKey == glfwKey)
                return mapping.EngineKey;
        }

        return std::nullopt;
    }

    std::optional<MouseButton> ToMouseButton(int glfwButton)
    {
        switch (glfwButton)
        {
            case GLFW_MOUSE_BUTTON_LEFT:   return MouseButton::Left;
            case GLFW_MOUSE_BUTTON_RIGHT:  return MouseButton::Right;
            case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
            case GLFW_MOUSE_BUTTON_4:      return MouseButton::Back;
            case GLFW_MOUSE_BUTTON_5:      return MouseButton::Forward;
            default:                       return std::nullopt;
        }
    }

    KeyModifiers ToModifiers(int glfwMods)
    {
        KeyModifiers modifiers = KeyModifiers::None;

        if (glfwMods & GLFW_MOD_SHIFT)   modifiers = modifiers | KeyModifiers::Shift;
        if (glfwMods & GLFW_MOD_CONTROL) modifiers = modifiers | KeyModifiers::Control;
        if (glfwMods & GLFW_MOD_ALT)     modifiers = modifiers | KeyModifiers::Alt;
        if (glfwMods & GLFW_MOD_SUPER)   modifiers = modifiers | KeyModifiers::Super;

        return modifiers;
    }
}
