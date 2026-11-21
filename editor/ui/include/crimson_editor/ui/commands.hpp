#pragma once
#include <functional>
#include <string>
#include <vector>

#include <crimson/core/keys.hpp>

namespace crimson::editor::ui
{
    // A key and the modifiers held with it
    struct Shortcut
    {
        crimson::Key Key;
        KeyModifiers Modifiers = KeyModifiers::None;
    };

    // How a shortcut reads in a menu: "Ctrl+Shift+Z", "F2", "Delete"
    [[nodiscard]] std::string ToText(const Shortcut& shortcut);

    // Something the user can do, from a menu, a shortcut or both. The workspace builds the main menu from the commands
    // and runs them when their keys are pressed. A command of a panel is in the menu while the panel is open, and its
    // shortcut works while the panel is focused. The ones the workspace is given work everywhere.
    struct Command
    {
        // Where it is in the main menu, nested by '/' as in "Edit/Rename". Empty for a shortcut without a menu item.
        std::string MenuPath;

        // The order in its menu, lowest first. A gap of more than 10 from the item before puts a separator between them.
        int Priority = 0;

        // The first one is shown in the menu, the others work too
        std::vector<Shortcut> Shortcuts;

        // Runs again while the keys are held
        bool Repeats = false;

        // Null for always. A command that isn't enabled is grayed out in the menu, and its shortcut does nothing.
        std::function<bool()> Enabled;

        std::function<void()> Run;
    };
}
