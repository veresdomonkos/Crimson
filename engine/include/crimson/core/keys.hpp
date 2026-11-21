#pragma once

#include <cstddef>
#include <cstdint>

namespace crimson
{
    // The keys of a keyboard, by what is printed on them in a US layout, whatever the layout in use is. Packed from 0, so
    // a key can index an array of kKeyCount.
    enum class Key
    {
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

        Space, Apostrophe, Comma, Minus, Period, Slash, Semicolon, Equal,
        LeftBracket, Backslash, RightBracket, GraveAccent,

        Escape, Enter, Tab, Backspace, Insert, Delete,
        Right, Left, Down, Up, PageUp, PageDown, Home, End,
        CapsLock, ScrollLock, NumLock, PrintScreen, Pause,

        Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract, KeypadAdd, KeypadEnter, KeypadEqual,

        LeftShift, LeftControl, LeftAlt, LeftSuper,
        RightShift, RightControl, RightAlt, RightSuper,
        Menu
    };

    // The last key is Menu. A key the window can't name is never sent, so there is no Unknown.
    constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Menu) + 1;

    enum class MouseButton
    {
        Left, Right, Middle, Back, Forward
    };

    constexpr std::size_t kMouseButtonCount = 5;

    // The modifier keys held while a key or button changed
    enum class KeyModifiers : std::uint8_t
    {
        None    = 0,
        Shift   = 1 << 0,
        Control = 1 << 1,
        Alt     = 1 << 2,
        Super   = 1 << 3,
    };

    inline KeyModifiers operator|(KeyModifiers a, KeyModifiers b)
    {
        return static_cast<KeyModifiers>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    inline KeyModifiers operator&(KeyModifiers a, KeyModifiers b)
    {
        return static_cast<KeyModifiers>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }

    inline bool HasModifier(KeyModifiers modifiers, KeyModifiers flag)
    {
        return (modifiers & flag) != KeyModifiers::None;
    }
}
